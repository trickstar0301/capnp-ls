// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include "symbol_index.h"
#include <capnp/schema.capnp.h>
#include <kj/map.h>

namespace capnp_ls {
class SymbolResolver {
public:
  static int resolve(capnp::schema::CodeGeneratorRequest::Reader request,
                     SymbolIndex &index,
                     const kj::Vector<kj::String> &importPaths,
                     const kj::StringPtr &workspacePath);
};
} // namespace capnp_ls
