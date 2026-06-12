// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <kj/common.h>
#include <kj/string.h>

namespace capnp_ls {

// Positions follow the internal 1-based convention (see lsp_json.cpp);
// character counts UTF-16 code units. replaceRange covers the digits already
// typed after '@' (an empty range at the cursor when none).
struct OrdinalCompletion {
  uint32_t nextOrdinal;
  Range replaceRange;
};

// Suggests the next field/method/enumerant ordinal when the cursor sits right
// after '@' (optionally followed by typed digits). Ordinal spaces: struct,
// interface and enum bodies own one each; union and group members share the
// enclosing struct's space. Returns none when the cursor is not in an ordinal
// position (no '@' before it, top level, inside a comment or string).
kj::Maybe<OrdinalCompletion> computeOrdinalCompletion(
    kj::StringPtr text,
    uint32_t line,
    uint32_t character);

} // namespace capnp_ls
