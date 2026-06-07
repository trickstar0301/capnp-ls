// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "symbol_resolver.h"
#include "kj_compat.h"
#include "logger.h"
#include <capnp/message.h>
#include <capnp/schema-loader.h>
#include <capnp/schema-parser.h>
#include <capnp/schema.capnp.h>
#include <capnp/serialize.h>
#include <fstream>
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/io.h>
#include <kj/map.h>
#include <kj/string-tree.h>
#include <kj/vector.h>
#include <sstream>

namespace capnp_ls {
struct BytePosition {
  uint32_t startByte;
  uint32_t endByte;
};

struct ByteRange {
  BytePosition startBytePosition;
  BytePosition endBytePosition;
};

struct TypeInfo {
  kj::Maybe<uint64_t> typeId;
  int listDepth;
  kj::String typeName;
};

Position getPositionInFile(kj::StringPtr filePath, size_t byteOffset) {
  auto fs = kj::newDiskFilesystem();

  const kj::Directory &rootDir = fs->getRoot();
  auto file = rootDir.openFile(kj::Path::parse(filePath.slice(1)));

  Position pos = {1, 1};

  if (byteOffset == 0) {
    return pos;
  }

  auto content = file->readAllText();

  for (size_t i = 0; i < byteOffset && i < content.size(); ++i) {
    if (content[i] == '\n') {
      pos.line++;
      pos.character = 1;
    } else {
      pos.character++;
    }
  }

  return pos;
}

kj::String extractFilePath(
    kj::StringPtr displayName,
    const kj::Vector<kj::String> &importPaths,
    kj::StringPtr workspacePath) {
  // extract file path from display name. if filename exists in import paths,
  // use it.

  KJ_LOG(INFO, "extractFilePath: ", displayName);
  auto colonPos = displayName.findFirst(':');
  kj::String relativeFilePathString;
  CAPNP_LS_IF_SOME (pos, colonPos) {
    relativeFilePathString = kj::heapString(displayName.slice(0, *pos));
  } else {
    relativeFilePathString = kj::heapString(displayName);
  }
  // Remove leading '/' if exists. kj::Path::parse() can only parse relative
  // path.
  if (relativeFilePathString.startsWith("/")) {
    relativeFilePathString = kj::heapString(relativeFilePathString.slice(1));
  }
  auto relativeFilePath = kj::Path::parse(relativeFilePathString);
  // Try workspace path first
  auto fs = kj::newDiskFilesystem();
  const kj::Directory &currentDir = fs->getCurrent();
  auto currentPath = fs->getCurrentPath();
  auto workspaceRelativePath =
      kj::Path::parse(workspacePath.startsWith("/")
                          ? workspacePath.slice(1)
                          : workspacePath);
  auto findInImportPath = [&](
                              kj::StringPtr importPath)
      -> kj::Maybe<kj::String> {
    if (importPath.startsWith("/")) {
      auto parsed = kj::Path::parse(importPath.slice(1));
      auto eval = parsed.eval(relativeFilePathString);
      if (fs->getRoot().exists(eval)) {
        KJ_LOG(INFO, "Found file in absolute import path", eval.toNativeString());
        return eval.toNativeString(true);
      }
    } else {
      auto workspaceImportPath =
          workspaceRelativePath.eval(importPath).eval(relativeFilePathString);
      if (fs->getRoot().exists(workspaceImportPath)) {
        KJ_LOG(
            INFO,
            "Found file in workspace-relative import path",
            workspaceImportPath.toNativeString());
        return workspaceImportPath.toNativeString(true);
      }
    }

    return CAPNP_LS_NONE;
  };

  auto workspaceFilePath = workspaceRelativePath.eval(relativeFilePathString);
  if (fs->getRoot().exists(workspaceFilePath)) {
    // exists in workspace
    KJ_LOG(INFO, "Found file in workspace", relativeFilePathString);
    return workspaceFilePath.toNativeString(true);
  } else if (currentDir.exists(relativeFilePath)) {
    // exists relative to current process directory
    KJ_LOG(INFO, "Found file in current directory", relativeFilePathString);
    return currentPath.eval(relativeFilePathString).toNativeString(true);
  } else {
    // Try import paths
    for (const auto &importPath : importPaths) {
      CAPNP_LS_IF_SOME (filePath, findInImportPath(importPath)) {
        return kj::mv(*filePath);
      }
    }

    CAPNP_LS_IF_SOME (
        filePath,
        findInImportPath(kj::str(CAPNP_LS_CAPNP_SOURCE_DIR, "/src"))) {
      return kj::mv(*filePath);
    }
  }
  // If file not found anywhere, throw exception
  KJ_FAIL_REQUIRE("File not found", relativeFilePath);
}

int SymbolResolver::resolve(
    capnp::schema::CodeGeneratorRequest::Reader request,
    kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> &positionToNodeIdMap,
    kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap,
    const kj::Vector<kj::String> &importPaths,
    const kj::StringPtr &workspacePath) {
  try {
    kj::HashMap<uint64_t, capnp::schema::Node::SourceInfo::Reader>
        sourceInfoMap;

    kj::HashMap<
        uint64_t,
        capnp::schema::CodeGeneratorRequest::RequestedFile::FileSourceInfo::
            Reader>
        fileSourceInfoMap;

    for (auto requestedFile : request.getRequestedFiles()) {
      fileSourceInfoMap.upsert(
          requestedFile.getId(), requestedFile.getFileSourceInfo());
    }

    capnp::SchemaLoader schemaLoader;
    for (auto node : request.getNodes()) {
      schemaLoader.load(node);
    }

    for (auto sourceInfo : request.getSourceInfo()) {
      sourceInfoMap.upsert(sourceInfo.getId(), sourceInfo);
    }

    int depth = 1;
    for (auto node : request.getNodes()) {
      if (node.which() == capnp::schema::Node::Which::FILE) {
        CAPNP_LS_IF_SOME (sourceInfo, fileSourceInfoMap.find(node.getId())) {
          kj::String filePath = extractFilePath(
              node.getDisplayName(), importPaths, workspacePath);
          // clear previous data for this file
          positionToNodeIdMap.erase(filePath);

          nodeLocationMap.upsert(
              node.getId(),
              kj::heap<Location>(Location{
                  kj::str(filePath), Range{Position{1, 1}, Position{1, 1}}}));

          for (auto identifier : sourceInfo->getIdentifiers()) {
            Range range{
                getPositionInFile(filePath, identifier.getStartByte()),
                getPositionInFile(filePath, identifier.getEndByte())};
            Location location{kj::str(filePath), range};
            auto &rangeMap = positionToNodeIdMap.findOrCreate(
                location.uri,
                [&]() -> kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>>::
                          Entry {
                            return {
                                kj::mv(location.uri),
                                kj::HashMap<Range, uint64_t>()};
                          });
            rangeMap.upsert(range, identifier.getTypeId());
          }
        }
        continue;
      }

      kj::StringPtr displayName = node.getDisplayName();
      if (displayName.endsWith("$Params") || displayName.endsWith("$Results")) {
        continue;
      }

      kj::String filePath =
          extractFilePath(displayName, importPaths, workspacePath);

      CAPNP_LS_IF_SOME (sourceInfo, sourceInfoMap.find(node.getId())) {
        Range range{
            getPositionInFile(filePath, sourceInfo->getStartByte()),
            getPositionInFile(filePath, sourceInfo->getEndByte())};
        nodeLocationMap.upsert(
            node.getId(),
            kj::heap<Location>(Location{kj::str(filePath), range}));
      }
    }
    // KJ_LOG(INFO, "positionToNodeIdMap:");
    // for (auto &[key, value] : positionToNodeIdMap) {
    //   KJ_LOG(INFO, key.cStr());
    //   for (auto &[range, nodeId] : value) {
    //     KJ_LOG(INFO, nodeId, range.start.line, range.start.character,
    //            range.end.character);
    //   }
    // }

    // KJ_LOG(INFO, "nodeLocationMap:");
    // for (auto &[key, value] : nodeLocationMap) {
    //   KJ_LOG(INFO, key, value->uri, value->range.start.line,
    //          value->range.end.line);
    // }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Failed to resolve symbols", e.getDescription());
    return 1;
  }
  return 0;
}
} // namespace capnp_ls
