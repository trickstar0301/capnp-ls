// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "compilation_manager.h"
#include "diagnostic_publisher.h"
#include "log_level.h"
#include "lsp_types.h"
#include "server_context.h"
#include "stdout_writer.h"
#include "symbol_index.h"
#include "utils.h"
#include "workspace_state.h"
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
#ifdef CAPNP_LS_TESTING
  kj::Promise<void> testHandleInitialize(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &initializeResponseBuilder) {
    return handleInitialize(params, initializeResponseBuilder);
  }
  size_t testImportPathCount() const {
    return workspace.importPaths.size();
  }
  kj::StringPtr testImportPath(size_t index) const {
    return workspace.importPaths[index];
  }
  kj::StringPtr testWorkspacePath() const {
    return workspace.workspacePath;
  }
  bool testReloadProjectConfig() {
    return workspace.reloadProjectConfig(index);
  }
  LogLevel testLogLevel() const {
    return workspace.currentLogLevel;
  }
#endif

private:
  kj::Promise<void> handleShutdown();
  kj::Promise<void> handleDefinition(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &definitionResponseBuilder);
  kj::Promise<void> handleHover(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &hoverResponseBuilder);
  kj::Promise<void> handleReferences(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &referencesResponseBuilder);
  kj::Promise<void> handleDocumentSymbol(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &documentSymbolResponseBuilder);
  kj::Promise<void>
  handleDidChangeWatchedFiles(const capnp::JsonValue::Reader &params);
  kj::Promise<void> handleDidSave(const capnp::JsonValue::Reader &params);
  kj::Promise<void> handleInitialize(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &initializeResponseBuilder);
  kj::Promise<void>
  handleDidOpenTextDocument(const capnp::JsonValue::Reader &params);
  kj::Promise<void>
  handleDidChangeTextDocument(const capnp::JsonValue::Reader &params);
  kj::Promise<void>
  handleDidCloseTextDocument(const capnp::JsonValue::Reader &params);
  kj::Promise<void> handleCompletion(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &completionResponseBuilder);
  kj::Promise<void> handleFormatting(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &formattingResponseBuilder);
  kj::Maybe<uint64_t>
  findNodeIdAtPosition(kj::StringPtr path, uint32_t line, uint32_t character);
  kj::Promise<void> dispatch(LspMethod method, const capnp::JsonValue::Reader &params, capnp::MallocMessageBuilder &response);

  SymbolIndex index;
  WorkspaceState workspace;
  // Editor buffers keyed by file path; the source of truth for features that
  // must work on unsaved text (completion).
  kj::HashMap<kj::String, kj::String> openDocuments;
  ServerContext &context;
  kj::Own<CompilationManager> compilationManager;
  StdoutWriter &stdoutWriter;
  DiagnosticPublisher diagnosticPublisher;
  kj::Promise<void> compileCapnpFile(kj::StringPtr uri);
};
} // namespace capnp_ls
