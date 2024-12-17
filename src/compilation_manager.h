// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

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
  };

  kj::Promise<void> compile(CompileParams params);

private:
  SubprocessRunner subprocessRunner;
  kj::Maybe<kj::String> buildCommand(CompileParams params);
};
} // namespace capnp_ls
