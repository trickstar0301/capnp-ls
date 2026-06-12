// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "linked_compiler.h"
#include "lsp_types.h"
#include "symbol_resolver.h"
#include <kj/async-io.h>
#include <kj/map.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

class CompilationManager {
public:
  explicit CompilationManager(kj::AsyncIoContext &ioContext);
  KJ_DISALLOW_COPY(CompilationManager);

  struct CompileParams {
    CompileParams(
        const kj::Vector<kj::String> &importPaths,
        kj::StringPtr fileName,
        kj::StringPtr workingDir,
        kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> &fileSourceInfoMap,
        kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap,
        kj::HashMap<uint64_t, kj::Own<SymbolMetadata>> &nodeMetadataMap,
        kj::HashMap<uint64_t, kj::Vector<Location>> &referenceMap,
        kj::HashMap<kj::String, kj::Vector<DocumentSymbol>> &documentSymbolMap,
        kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap)
        : importPaths(importPaths),
          fileName(fileName),
          workingDir(workingDir),
          fileSourceInfoMap(fileSourceInfoMap),
          nodeLocationMap(nodeLocationMap),
          nodeMetadataMap(nodeMetadataMap),
          referenceMap(referenceMap),
          documentSymbolMap(documentSymbolMap),
          diagnosticMap(diagnosticMap) {}

    const kj::Vector<kj::String> &importPaths;
    kj::StringPtr fileName;
    kj::StringPtr workingDir;
    kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> &fileSourceInfoMap;
    kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap;
    kj::HashMap<uint64_t, kj::Own<SymbolMetadata>> &nodeMetadataMap;
    kj::HashMap<uint64_t, kj::Vector<Location>> &referenceMap;
    kj::HashMap<kj::String, kj::Vector<DocumentSymbol>> &documentSymbolMap;
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap;
  };

  struct FormatParams {
    kj::StringPtr fileName;
    kj::StringPtr workingDir;
  };

  kj::Promise<void> compile(CompileParams params);
  kj::Promise<void> format(FormatParams params);
};
} // namespace capnp_ls
