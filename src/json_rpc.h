// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <kj/string.h>

namespace capnp_ls {

kj::String frameLspMessage(kj::StringPtr json);

kj::Maybe<kj::String>
buildResponseString(const double id, const capnp::JsonValue::Reader &result);

kj::Maybe<kj::String>
buildErrorResponseString(const double id, int code, kj::StringPtr message);

} // namespace capnp_ls
