// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <kj/map.h>
#include <kj/memory.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

struct SymbolIndex {
  KJ_DISALLOW_COPY(SymbolIndex);

  SymbolIndex() = default;

  kj::HashMap<kj::String, kj::HashMap<Range, uint64_t>> fileSourceInfoMap;
  kj::HashMap<uint64_t, kj::Own<Location>> nodeLocationMap;
  kj::HashMap<uint64_t, kj::Own<SymbolMetadata>> nodeMetadataMap;
  kj::HashMap<uint64_t, kj::Vector<Location>> referenceMap;
  kj::HashMap<kj::String, kj::Vector<DocumentSymbol>> documentSymbolMap;
  kj::HashMap<kj::String, kj::Vector<Diagnostic>> diagnosticMap;

  void clear();
  kj::Maybe<uint64_t> findNodeIdAtPosition(kj::StringPtr path, uint32_t line, uint32_t character) const;
};

} // namespace capnp_ls
