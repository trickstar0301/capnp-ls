// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include "subprocess_runner.h"
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
    kj::StringPtr compilerPath;
    const kj::Vector<kj::String> &importPaths;
    kj::StringPtr fileName;
    kj::StringPtr workingDir;
    kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> &fileSourceInfoMap;
    kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap;
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap;
  };

  struct FormatParams {
    kj::StringPtr compilerPath;
    kj::StringPtr fileName;
    kj::StringPtr workingDir;
  };

  kj::Promise<void> compile(CompileParams params);
  kj::Promise<bool> checkCapnpVersionCompatible(kj::StringPtr compilerPath);
  kj::Promise<void> format(FormatParams params);

private:
  SubprocessRunner subprocessRunner;
  kj::Maybe<kj::String> buildCommand(CompileParams params);
  bool isCapnpVersionCompatible = false;
};
} // namespace capnp_ls
