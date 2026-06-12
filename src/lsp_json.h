// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <kj/string.h>

namespace capnp_ls {

struct TextDocumentPosition {
  kj::String uri;
  kj::String path;
  uint32_t line = 0;
  uint32_t character = 0;
};

void setPosition(capnp::JsonValue::Builder value, const Position &position);
void setRange(capnp::JsonValue::Builder value, const Range &range);
void setLocation(capnp::JsonValue::Builder value, const Location &location);

TextDocumentPosition parseTextDocumentPosition(
    const capnp::JsonValue::Reader &params);
kj::String parseTextDocumentPath(const capnp::JsonValue::Reader &params);

} // namespace capnp_ls
