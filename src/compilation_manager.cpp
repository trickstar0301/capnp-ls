// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "compilation_manager.h"
#include "compile_error_parser.h"
#include "utils.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string.h>

namespace capnp_ls {

CompilationManager::CompilationManager(kj::AsyncIoContext &ioContext)
    : subprocessRunner(ioContext) {}

kj::Promise<void> CompilationManager::compile(CompileParams params) {
  KJ_LOG(INFO, "Compiling:", params.fileName);
  kj::String strippedUri = kj::heapString(params.fileName);
  if (strippedUri.startsWith(params.workingDir)) {
    strippedUri =
        kj::heapString(strippedUri.slice(params.workingDir.size() + 1));
  }
  params.diagnosticMap.clear();
  KJ_IF_MAYBE (command, buildCommand(params)) {
    return subprocessRunner
        .run({.command = *command, .workingDir = params.workingDir})
        .then([params, fileName = kj::mv(strippedUri)](
                  SubprocessRunner::RunResult result) mutable {
          if (result.exitCode != 0) {
            int status = CompileErrorParser::parse(
                fileName, result.errorText, params.diagnosticMap);
            if (status != 0) {
              KJ_LOG(ERROR, "Failed to parse compile errors");
            }
            return kj::Promise<void>(kj::READY_NOW);
          }

          KJ_IF_MAYBE (reader, result.maybeReader) {
            SymbolResolver::resolve(
                kj::mv(*reader),
                params.fileSourceInfoMap,
                params.nodeLocationMap,
                params.importPaths,
                params.workingDir);
          }
          return kj::Promise<void>(kj::READY_NOW);
        })
        .catch_([](kj::Exception &&e) {
          KJ_LOG(ERROR, "Compilation error, exception:", e.getDescription());
          return kj::Promise<void>(kj::READY_NOW);
        });
  }
  return kj::Promise<void>(kj::READY_NOW);
}

kj::Maybe<kj::String> CompilationManager::buildCommand(CompileParams params) {
  kj::Vector<kj::String> args;
  if (!params.compilerPath.endsWith("capnp")) {
    KJ_LOG(ERROR, "Compiler path must end with 'capnp'");
    return nullptr;
  }
  args.add(kj::heapString(params.compilerPath));
  args.add(kj::heapString("compile"));

  for (auto &path : params.importPaths) {
    args.add(kj::str("-I", path));
  }

  args.add(kj::str("-o", "-")); // output to stdout
  args.add(kj::heapString(params.fileName));

  kj::String result;
  for (auto &arg : args) {
    if (result.size() > 0)
      result = kj::str(result, " ");
    if (arg.findFirst(' ') != nullptr || arg.findFirst('\t') != nullptr) {
      result = kj::str(result, "\"", arg, "\"");
    } else {
      result = kj::str(result, arg);
    }
  }

  return result;
}
} // namespace capnp_ls