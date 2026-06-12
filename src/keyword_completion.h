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
// character counts UTF-16 code units. keywords point at static storage.
// replaceRange covers the identifier prefix already typed (an empty range at
// the cursor when none).
struct KeywordCompletion {
  kj::ArrayPtr<const kj::StringPtr> keywords;
  Range replaceRange;
};

// Suggests declaration keywords when the cursor is typing the first word of a
// statement. The candidate set depends on the enclosing block: file scope and
// interface bodies offer declaration keywords, struct bodies additionally
// offer "union", union/group bodies offer only "union", and enum bodies offer
// nothing. Returns none mid-statement, inside comments or strings, and right
// after '@' (an ordinal position).
kj::Maybe<KeywordCompletion> computeKeywordCompletion(
    kj::StringPtr text,
    uint32_t line,
    uint32_t character);

} // namespace capnp_ls
