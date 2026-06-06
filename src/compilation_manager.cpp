// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "compilation_manager.h"
#include <kj/debug.h>

namespace capnp_ls {

CompilationManager::CompilationManager(kj::AsyncIoContext &ioContext) {
  (void)ioContext;
}

kj::Promise<void> CompilationManager::compile(CompileParams params) {
  try {
    KJ_LOG(INFO, "Compiling:", params.fileName);

    auto result = LinkedCompiler::compile(
        params.fileName,
        params.workingDir,
        params.importPaths,
        params.diagnosticMap);

    if (!result.success) {
      KJ_LOG(INFO, "Compilation produced diagnostics", params.fileName);
      return kj::READY_NOW;
    }

    KJ_IF_MAYBE (requestMessage, result.request) {
      auto request =
          (*requestMessage)->getRoot<capnp::schema::CodeGeneratorRequest>();
      SymbolResolver::resolve(
          request,
          params.fileSourceInfoMap,
          params.nodeLocationMap,
          params.importPaths,
          params.workingDir);
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Compilation error, exception:", e.getDescription());
  }

  return kj::READY_NOW;
}

} // namespace capnp_ls
