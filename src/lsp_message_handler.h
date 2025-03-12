// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "compilation_manager.h"
#include "lsp_types.h"
#include "server_context.h"
#include "stdout_writer.h"
#include "utils.h"
#include <capnp/compat/json.h>
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/map.h>
#include <kj/memory.h>
#include <kj/string.h>

namespace capnp_ls {

class LspMessageHandler {
public:
  LspMessageHandler(ServerContext &serverContext, StdoutWriter &stdoutWriter);
  kj::Promise<void> handleMessage(kj::Maybe<kj::String> message);

private:
  kj::Maybe<kj::String>
  buildResponseString(const double id, const capnp::JsonValue::Reader &result);
  kj::Promise<void> handleShutdown();
  kj::Promise<void> handleDefinition(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &definitionResponseBuilder);
  kj::Promise<void>
  handleDidChangeWatchedFiles(const capnp::JsonValue::Reader &params);
  kj::Promise<void> handleDidSave(const capnp::JsonValue::Reader &params);
  kj::Promise<void> handleInitialize(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &initializeResponseBuilder);
  kj::Promise<void>
  handleDidOpenTextDocument(const capnp::JsonValue::Reader &params);
  kj::Promise<void> publishDiagnostics(kj::StringPtr fileName);

  kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> fileSourceInfoMap;
  kj::HashMap<uint64_t, kj::Own<Location>> nodeLocationMap;
  kj::HashMap<kj::String, kj::Vector<Diagnostic>> diagnosticMap;
  kj::String workspacePath;
  kj::String compilerPath;
  kj::Vector<kj::String> importPaths;
  ServerContext &context;
  kj::Own<CompilationManager> compilationManager;
  StdoutWriter &stdoutWriter;
  kj::Promise<void> compileCapnpFile(kj::StringPtr uri);
};
} // namespace capnp_ls