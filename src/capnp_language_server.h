// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "compilation_manager.h"
#include "language_server.h"
#include "lsp_types.h"
#include "server_context.h"
#include "utils.h"
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/map.h>
#include <kj/string.h>

namespace capnp_ls {

class CapnpLanguageServer : public LanguageServer {
private:
  kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> fileSourceInfoMap;
  kj::HashMap<uint64_t, kj::Own<Location>> nodeLocationMap;
  kj::String workspacePath;
  kj::String compilerPath;
  kj::Vector<kj::String> importPaths;
  ServerContext &context;
  kj::Own<CompilationManager> compilationManager;

  kj::Promise<void> compileCapnpFile(kj::StringPtr uri);

public:
  explicit CapnpLanguageServer(ServerContext &serverContext);
  ~CapnpLanguageServer() noexcept override = default;

  kj::Promise<void> onShutdown() override;
  kj::Promise<void> onDefinition(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &definitionResponseBuilder) override;
  kj::Promise<void>
  onDidChangeWatchedFiles(const capnp::JsonValue::Reader &params) override;
  kj::Promise<void> onDidSave(const capnp::JsonValue::Reader &params) override;
  kj::Promise<void> onInitialize(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &initializeResponseBuilder) override;
  kj::Promise<void>
  onDidOpenTextDocument(const capnp::JsonValue::Reader &params) override;
};

} // namespace capnp_ls
