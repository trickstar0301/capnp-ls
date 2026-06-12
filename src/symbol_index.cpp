// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "symbol_index.h"
#include "kj_compat.h"
#include "lsp_types.h"

namespace capnp_ls {

void SymbolIndex::clear() {
  fileSourceInfoMap.clear();
  nodeLocationMap.clear();
  nodeMetadataMap.clear();
  referenceMap.clear();
  documentSymbolMap.clear();
  diagnosticMap.clear();
}

kj::Maybe<uint64_t> SymbolIndex::findNodeIdAtPosition(
    kj::StringPtr path,
    uint32_t line,
    uint32_t character) const {
  CAPNP_LS_IF_SOME (rangeMap, fileSourceInfoMap.find(path)) {
    for (const auto &[range, id] : *rangeMap) {
      if (containsPosition(range, line, character)) {
        return id;
      }
    }
  }

  // Fall back to declaration ranges, picking the most deeply nested one.
  kj::Maybe<uint64_t> bestId = CAPNP_LS_NONE;
  const Range *bestRange = nullptr;
  for (const auto &[id, location] : nodeLocationMap) {
    if (location->uri != path ||
        !containsPosition(location->range, line, character)) {
      continue;
    }
    if (bestRange == nullptr || isTighterRange(location->range, *bestRange)) {
      bestId = id;
      bestRange = &location->range;
    }
  }
  return bestId;
}

} // namespace capnp_ls
