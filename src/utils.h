// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <cstddef>
#include <cstdint>
#include <kj/common.h>
#include <kj/string.h>

namespace capnp_ls {
kj::String uriToPath(const kj::StringPtr uri);
kj::String pathToUri(const kj::StringPtr path);

// Byte offset of an internal 1-based position. character counts UTF-16 code
// units (LSP wire convention) and is clamped to the end of the line.
kj::Maybe<size_t> byteOffsetForPosition(
    kj::StringPtr text,
    uint32_t line,
    uint32_t character);
}
