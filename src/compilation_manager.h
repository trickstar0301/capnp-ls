// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "linked_compiler.h"
#include "lsp_types.h"
#include "symbol_index.h"
#include "symbol_resolver.h"
#include <kj/async-io.h>
#include <kj/map.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

class CompilationManager {
public:
  CompilationManager() = default;
  KJ_DISALLOW_COPY(CompilationManager);

  struct CompileParams {
    CompileParams(
        const kj::Vector<kj::String> &importPaths,
        kj::StringPtr fileName,
        kj::StringPtr workingDir,
        SymbolIndex &index)
        : importPaths(importPaths),
          fileName(fileName),
          workingDir(workingDir),
          index(index) {}

    const kj::Vector<kj::String> &importPaths;
    kj::StringPtr fileName;
    kj::StringPtr workingDir;
    SymbolIndex &index;
  };

  kj::Promise<void> compile(CompileParams params);
};
} // namespace capnp_ls
