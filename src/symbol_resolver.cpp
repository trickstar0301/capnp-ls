// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "symbol_resolver.h"
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
  const kj::Directory &currentDir = fs->getCurrent();
  auto file = currentDir.openFile(kj::Path::parse(filePath));

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

kj::String extractFilePath(kj::StringPtr displayName,
                           const kj::Vector<kj::String> &importPaths,
                           kj::StringPtr workspacePath) {
  // extract file path from display name. if filename exists in import paths,
  // use it.

  KJ_LOG(INFO, "extractFilePath: ", displayName);
  auto colonPos = displayName.findFirst(':');
  kj::String filePath;
  KJ_IF_MAYBE (pos, colonPos) {
    filePath = kj::heapString(displayName.slice(0, *pos));
  } else {
    filePath = kj::heapString(displayName);
  }

  // Remove leading '/' if exists. kj::Path::parse() can only parse relative
  // path.
  if (filePath.size() > 0 && filePath[0] == '/') {
    filePath = kj::heapString(filePath.slice(1));
  }

  // Try workspace path first
  auto fs = kj::newDiskFilesystem();
  const kj::Directory &currentDir = fs->getCurrent();

  try {
    if (currentDir.exists(kj::Path::parse(filePath))) {
      KJ_LOG(INFO, "Found file in current directory", filePath);
      return kj::mv(filePath);
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Failed to check current directory", e.getDescription());
  }

  // If not found in workspace, try each import path
  for (const auto &importPath : importPaths) {
    auto withImportPath = kj::Path::parse(importPath);
    withImportPath = withImportPath.evalNative(filePath);
    try {
      if (currentDir.exists(withImportPath)) {
        KJ_LOG(INFO, "Found file in import path", withImportPath);
        return kj::str(withImportPath);
      }
    } catch (kj::Exception &e) {
      KJ_LOG(ERROR, "Failed to check import path", e.getDescription());
    }
  }

  // If file not found anywhere, throw exception
  KJ_FAIL_REQUIRE("File not found", filePath);
}

int SymbolResolver::resolve(
    kj::Own<capnp::MessageReader> reader,
    kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> &positionToNodeIdMap,
    kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap,
    const kj::Vector<kj::String> &importPaths,
    const kj::StringPtr &workspacePath) {
  try {
    kj::HashMap<uint64_t, capnp::schema::Node::SourceInfo::Reader>
        sourceInfoMap;

    auto request = reader->getRoot<capnp::schema::CodeGeneratorRequest>();

    kj::HashMap<uint64_t, capnp::schema::CodeGeneratorRequest::RequestedFile::
                              FileSourceInfo::Reader>
        fileSourceInfoMap;

    for (auto requestedFile : request.getRequestedFiles()) {
      fileSourceInfoMap.upsert(requestedFile.getId(),
                               requestedFile.getFileSourceInfo());
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
        KJ_IF_MAYBE (sourceInfo, fileSourceInfoMap.find(node.getId())) {
          kj::String filePath = extractFilePath(node.getDisplayName(),
                                                importPaths, workspacePath);
          // clear previous data for this file
          positionToNodeIdMap.erase(filePath);

          nodeLocationMap.upsert(
              node.getId(),
              kj::heap<Location>(Location{
                  kj::str(filePath), Range{Position{1, 1}, Position{1, 1}}}));

          for (auto identifier : sourceInfo->getIdentifiers()) {
            Range range{getPositionInFile(filePath, identifier.getStartByte()),
                        getPositionInFile(filePath, identifier.getEndByte())};
            Location location{kj::str(filePath), range};
            auto &rangeMap = positionToNodeIdMap.findOrCreate(
                location.uri,
                [&]() -> kj::HashMap<kj::String,
                                     kj::HashMap<Range, uint64_t>>::Entry {
                  return {kj::mv(location.uri), kj::HashMap<Range, uint64_t>()};
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

      KJ_IF_MAYBE (sourceInfo, sourceInfoMap.find(node.getId())) {
        Range range{getPositionInFile(filePath, sourceInfo->getStartByte()),
                    getPositionInFile(filePath, sourceInfo->getEndByte())};
        nodeLocationMap.upsert(node.getId(), kj::heap<Location>(Location{
                                                 kj::str(filePath), range}));
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