// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <capnp/message.h>
#include <kj/map.h>

namespace capnp_ls {
class SymbolResolver {
public:
  static int resolve(kj::Own<capnp::MessageReader> reader,
                     kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>>
                         &positionToNodeIdMap,
                     kj::HashMap<uint64_t, kj::Own<Location>> &nodeLocationMap,
                     const kj::Vector<kj::String> &importPaths,
                     const kj::StringPtr &workspacePath);
};
} // namespace capnp_ls