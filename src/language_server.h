// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <capnp/compat/json.h>

namespace capnp_ls {

class LanguageServer {
public:
  virtual kj::Promise<void> onInitialize(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &initializeResponseBuilder) = 0;
  virtual kj::Promise<void> onDefinition(
      const capnp::JsonValue::Reader &params,
      capnp::MallocMessageBuilder &definitionResponseBuilder) = 0;
  virtual kj::Promise<void>
  onDidChangeWatchedFiles(const capnp::JsonValue::Reader &params) = 0;
  virtual kj::Promise<void>
  onDidOpenTextDocument(const capnp::JsonValue::Reader &params) = 0;
  virtual kj::Promise<void> onShutdown() = 0;
  virtual kj::Promise<void>
  onDidSave(const capnp::JsonValue::Reader &params) = 0;
  virtual ~LanguageServer() = default;
};
} // namespace capnp_ls