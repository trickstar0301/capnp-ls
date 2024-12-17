// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "language_server.h"
#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <kj/memory.h>
#include <kj/string.h>

namespace capnp_ls {

class LspMessageHandler {
public:
  LspMessageHandler(kj::Own<LanguageServer> server_)
      : server(kj::mv(server_)) {}

  kj::Promise<kj::Maybe<kj::String>>
  handleMessage(kj::Maybe<kj::StringPtr> message);

private:
  kj::Maybe<kj::String>
  buildResponseString(const double id, const capnp::JsonValue::Reader &result);
  kj::Own<LanguageServer> server;
};
} // namespace capnp_ls