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

// Converts byte offsets to positions, reading each file once per instance.
class PositionCalculator {
public:
  explicit PositionCalculator(kj::Filesystem &filesystem)
      : filesystem(filesystem) {}

  Position getPosition(kj::StringPtr filePath, size_t byteOffset) {
    auto &table = lineTables.findOrCreate(
        filePath,
        [&]() -> kj::HashMap<kj::String, LineTable>::Entry {
          return {kj::heapString(filePath), buildLineTable(filePath)};
        });
    size_t offset = kj::min(byteOffset, table.contentSize);
    size_t lineIndex = 0;
    size_t end = table.lineStarts.size();
    while (lineIndex + 1 < end) {
      size_t mid = lineIndex + (end - lineIndex) / 2;
      if (table.lineStarts[mid] <= offset) {
        lineIndex = mid;
      } else {
        end = mid;
      }
    }
    return Position{
        static_cast<uint32_t>(lineIndex + 1),
        static_cast<uint32_t>(offset - table.lineStarts[lineIndex] + 1)};
  }

private:
  struct LineTable {
    kj::Vector<size_t> lineStarts;
    size_t contentSize;
  };

  LineTable buildLineTable(kj::StringPtr filePath) {
    auto file = filesystem.getRoot().openFile(kj::Path::parse(filePath.slice(1)));
    auto content = file->readAllText();
    kj::Vector<size_t> lineStarts;
    lineStarts.add(0);
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == '\n') {
        lineStarts.add(i + 1);
      }
    }
    return LineTable{kj::mv(lineStarts), content.size()};
  }

  kj::Filesystem &filesystem;
  kj::HashMap<kj::String, LineTable> lineTables;
};

constexpr uint32_t LSP_SYMBOL_KIND_FILE = 1;
constexpr uint32_t LSP_SYMBOL_KIND_METHOD = 6;
constexpr uint32_t LSP_SYMBOL_KIND_FIELD = 8;
constexpr uint32_t LSP_SYMBOL_KIND_ENUM = 10;
constexpr uint32_t LSP_SYMBOL_KIND_INTERFACE = 11;
constexpr uint32_t LSP_SYMBOL_KIND_CONSTANT = 14;
constexpr uint32_t LSP_SYMBOL_KIND_ENUM_MEMBER = 22;
constexpr uint32_t LSP_SYMBOL_KIND_STRUCT = 23;

kj::String shortDisplayName(capnp::schema::Node::Reader node) {
  auto displayName = node.getDisplayName();
  auto prefixLength = node.getDisplayNamePrefixLength();
  if (prefixLength <= displayName.size()) {
    return kj::heapString(displayName.slice(prefixLength));
  }
  return kj::heapString(displayName);
}

uint32_t documentSymbolKind(capnp::schema::Node::Reader node) {
  switch (node.which()) {
  case capnp::schema::Node::Which::FILE:
    return LSP_SYMBOL_KIND_FILE;
  case capnp::schema::Node::Which::STRUCT:
    return LSP_SYMBOL_KIND_STRUCT;
  case capnp::schema::Node::Which::ENUM:
    return LSP_SYMBOL_KIND_ENUM;
  case capnp::schema::Node::Which::INTERFACE:
    return LSP_SYMBOL_KIND_INTERFACE;
  case capnp::schema::Node::Which::CONST:
    return LSP_SYMBOL_KIND_CONSTANT;
  case capnp::schema::Node::Which::ANNOTATION:
    return LSP_SYMBOL_KIND_CONSTANT;
  default:
    return LSP_SYMBOL_KIND_FIELD;
  }
}

kj::String nodeKindName(capnp::schema::Node::Reader node) {
  switch (node.which()) {
  case capnp::schema::Node::Which::FILE:
    return kj::heapString("file");
  case capnp::schema::Node::Which::STRUCT:
    return node.getStruct().getIsGroup() ? kj::heapString("group")
                                         : kj::heapString("struct");
  case capnp::schema::Node::Which::ENUM:
    return kj::heapString("enum");
  case capnp::schema::Node::Which::INTERFACE:
    return kj::heapString("interface");
  case capnp::schema::Node::Which::CONST:
    return kj::heapString("const");
  case capnp::schema::Node::Which::ANNOTATION:
    return kj::heapString("annotation");
  default:
    return kj::heapString("symbol");
  }
}

void addReference(
    kj::HashMap<uint64_t, kj::Vector<Location>> &referenceMap,
    uint64_t nodeId,
    kj::StringPtr uri,
    const Range &range) {
  auto &references = referenceMap.findOrCreate(
      nodeId,
      [&]() -> kj::HashMap<uint64_t, kj::Vector<Location>>::Entry {
        return {nodeId, kj::Vector<Location>()};
      });
  // Imported files are not cleared on recompile, so skip locations that are
  // already recorded.
  for (const auto &reference : references) {
    if (reference.uri == uri && reference.range == range) {
      return;
    }
  }
  references.add(Location{kj::heapString(uri), range});
}

void addDocumentSymbol(
    kj::HashMap<kj::String, kj::Vector<DocumentSymbol>> &documentSymbolMap,
    kj::StringPtr filePath,
    DocumentSymbol symbol) {
  auto &symbols = documentSymbolMap.findOrCreate(
      filePath,
      [&]() -> kj::HashMap<kj::String, kj::Vector<DocumentSymbol>>::Entry {
        return {kj::heapString(filePath), kj::Vector<DocumentSymbol>()};
      });
  symbols.add(kj::mv(symbol));
}

void removeReferencesInFile(
    kj::HashMap<uint64_t, kj::Vector<Location>> &referenceMap,
    kj::StringPtr filePath) {
  for (auto &[_, references] : referenceMap) {
    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < references.size(); ++readIndex) {
      if (references[readIndex].uri != filePath) {
        if (writeIndex != readIndex) {
          references[writeIndex] = kj::mv(references[readIndex]);
        }
        ++writeIndex;
      }
    }
    references.truncate(writeIndex);
  }
}

void addMemberDocumentSymbols(
    capnp::schema::Node::Reader node,
    capnp::schema::Node::SourceInfo::Reader sourceInfo,
    kj::StringPtr filePath,
    kj::HashMap<kj::String, kj::Vector<DocumentSymbol>> &documentSymbolMap,
    PositionCalculator &positionCalculator) {
  auto members = sourceInfo.getMembers();

  if (node.which() == capnp::schema::Node::Which::STRUCT) {
    auto fields = node.getStruct().getFields();
    auto size = kj::min(fields.size(), members.size());
    for (uint i = 0; i < size; ++i) {
      Range range{
          positionCalculator.getPosition(filePath, members[i].getStartByte()),
          positionCalculator.getPosition(filePath, members[i].getEndByte())};
      addDocumentSymbol(
          documentSymbolMap,
          filePath,
          DocumentSymbol{
              kj::heapString(fields[i].getName()),
              kj::heapString("field"),
              LSP_SYMBOL_KIND_FIELD,
              range,
              range});
    }
  } else if (node.which() == capnp::schema::Node::Which::ENUM) {
    auto enumerants = node.getEnum().getEnumerants();
    auto size = kj::min(enumerants.size(), members.size());
    for (uint i = 0; i < size; ++i) {
      Range range{
          positionCalculator.getPosition(filePath, members[i].getStartByte()),
          positionCalculator.getPosition(filePath, members[i].getEndByte())};
      addDocumentSymbol(
          documentSymbolMap,
          filePath,
          DocumentSymbol{
              kj::heapString(enumerants[i].getName()),
              kj::heapString("enumerant"),
              LSP_SYMBOL_KIND_ENUM_MEMBER,
              range,
              range});
    }
  } else if (node.which() == capnp::schema::Node::Which::INTERFACE) {
    auto methods = node.getInterface().getMethods();
    auto size = kj::min(methods.size(), members.size());
    for (uint i = 0; i < size; ++i) {
      Range range{
          positionCalculator.getPosition(filePath, members[i].getStartByte()),
          positionCalculator.getPosition(filePath, members[i].getEndByte())};
      addDocumentSymbol(
          documentSymbolMap,
          filePath,
          DocumentSymbol{
              kj::heapString(methods[i].getName()),
              kj::heapString("method"),
              LSP_SYMBOL_KIND_METHOD,
              range,
              range});
    }
  }
}

kj::String extractFilePath(
    kj::StringPtr displayName,
    kj::Filesystem &fs,
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
  const kj::Directory &currentDir = fs.getCurrent();
  auto currentPath = fs.getCurrentPath();
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
      if (fs.getRoot().exists(eval)) {
        KJ_LOG(INFO, "Found file in absolute import path", eval.toNativeString());
        return eval.toNativeString(true);
      }
    } else {
      auto workspaceImportPath =
          workspaceRelativePath.eval(importPath).eval(relativeFilePathString);
      if (fs.getRoot().exists(workspaceImportPath)) {
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
  if (fs.getRoot().exists(workspaceFilePath)) {
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

template <typename ResolveFilePath>
void clearStalePerFileState(
    capnp::schema::CodeGeneratorRequest::Reader request,
    SymbolIndex &index,
    const ResolveFilePath &resolveFilePath,
    const kj::HashMap<
        uint64_t,
        capnp::schema::CodeGeneratorRequest::RequestedFile::FileSourceInfo::
            Reader> &fileSourceInfoMap) {
  // Clear stale per-file state before adding anything: node order in the
  // request is not guaranteed, so clearing while adding could wipe data
  // that was just added for the same file.
  for (auto node : request.getNodes()) {
    if (node.which() == capnp::schema::Node::Which::FILE) {
      CAPNP_LS_IF_SOME (sourceInfo, fileSourceInfoMap.find(node.getId())) {
        kj::StringPtr filePath = resolveFilePath(node.getDisplayName());
        index.fileSourceInfoMap.erase(filePath);
        index.documentSymbolMap.erase(filePath);
        // Only requested files lose their references. Imported files keep
        // theirs: identifier usages inside an imported file are only
        // re-added when that file is compiled as a requested file.
        removeReferencesInFile(index.referenceMap, filePath);
      }
      continue;
    }
    kj::StringPtr displayName = node.getDisplayName();
    if (displayName.endsWith("$Params") || displayName.endsWith("$Results")) {
      continue;
    }
    // Document symbols for this file are fully re-added below, including
    // for imported files.
    index.documentSymbolMap.erase(resolveFilePath(displayName));
  }
}

void indexFileNode(
    capnp::schema::Node::Reader node,
    SymbolIndex &index,
    PositionCalculator &positionCalculator,
    kj::StringPtr filePath,
    capnp::schema::CodeGeneratorRequest::RequestedFile::FileSourceInfo::Reader
        sourceInfo) {
  index.nodeLocationMap.upsert(
      node.getId(),
      kj::heap<Location>(Location{
          kj::str(filePath), Range{Position{1, 1}, Position{1, 1}}}));
  index.nodeMetadataMap.upsert(
      node.getId(),
      kj::heap<SymbolMetadata>(SymbolMetadata{
          shortDisplayName(node),
          nodeKindName(node),
          kj::heapString("")}));

  for (auto identifier : sourceInfo.getIdentifiers()) {
    Range range{
        positionCalculator.getPosition(
            filePath, identifier.getStartByte()),
        positionCalculator.getPosition(
            filePath, identifier.getEndByte())};
    Location location{kj::str(filePath), range};
    addReference(index.referenceMap, identifier.getTypeId(), filePath, range);
    auto &rangeMap = index.fileSourceInfoMap.findOrCreate(
        location.uri,
        [&]() -> kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>>::Entry {
          return {kj::mv(location.uri), kj::HashMap<Range, uint64_t>()};
        });
    rangeMap.upsert(range, identifier.getTypeId());
  }
}

void indexDeclarationNode(
    capnp::schema::Node::Reader node,
    SymbolIndex &index,
    PositionCalculator &positionCalculator,
    kj::StringPtr filePath,
    capnp::schema::Node::SourceInfo::Reader sourceInfo) {
  Range range{
      positionCalculator.getPosition(filePath, sourceInfo.getStartByte()),
      positionCalculator.getPosition(filePath, sourceInfo.getEndByte())};
  index.nodeLocationMap.upsert(
      node.getId(),
      kj::heap<Location>(Location{kj::str(filePath), range}));
  index.nodeMetadataMap.upsert(
      node.getId(),
      kj::heap<SymbolMetadata>(SymbolMetadata{
          shortDisplayName(node),
          nodeKindName(node),
          kj::heapString(sourceInfo.getDocComment())}));
  addReference(index.referenceMap, node.getId(), filePath, range);
  addDocumentSymbol(
      index.documentSymbolMap,
      filePath,
      DocumentSymbol{
          shortDisplayName(node),
          nodeKindName(node),
          documentSymbolKind(node),
          range,
          range});
  addMemberDocumentSymbols(
      node, sourceInfo, filePath, index.documentSymbolMap, positionCalculator);
}

int SymbolResolver::resolve(
    capnp::schema::CodeGeneratorRequest::Reader request,
    kj::Filesystem &filesystem,
    SymbolIndex &index,
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

    PositionCalculator positionCalculator(filesystem);

    // extractFilePath probes the filesystem, so resolve each display name
    // prefix only once per request.
    kj::HashMap<kj::String, kj::String> filePathCache;
    auto resolveFilePath = [&](kj::StringPtr displayName) -> kj::StringPtr {
      kj::String key;
      CAPNP_LS_IF_SOME (pos, displayName.findFirst(':')) {
        key = kj::heapString(displayName.slice(0, *pos));
      } else {
        key = kj::heapString(displayName);
      }
      return filePathCache.findOrCreate(
          key,
          [&]() -> kj::HashMap<kj::String, kj::String>::Entry {
            return {
                kj::mv(key),
                extractFilePath(displayName, filesystem, importPaths, workspacePath)};
          });
    };

    clearStalePerFileState(request, index, resolveFilePath, fileSourceInfoMap);

    for (auto node : request.getNodes()) {
      if (node.which() == capnp::schema::Node::Which::FILE) {
        CAPNP_LS_IF_SOME (sourceInfo, fileSourceInfoMap.find(node.getId())) {
          kj::StringPtr filePath = resolveFilePath(node.getDisplayName());
          indexFileNode(node, index, positionCalculator, filePath, *sourceInfo);
        }
        continue;
      }

      kj::StringPtr displayName = node.getDisplayName();
      if (displayName.endsWith("$Params") || displayName.endsWith("$Results")) {
        continue;
      }

      kj::StringPtr filePath = resolveFilePath(displayName);

      CAPNP_LS_IF_SOME (sourceInfo, sourceInfoMap.find(node.getId())) {
        indexDeclarationNode(node, index, positionCalculator, filePath, *sourceInfo);
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Failed to resolve symbols", e.getDescription());
    return 1;
  }
  return 0;
}
} // namespace capnp_ls
