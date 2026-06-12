// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_types.h"
#include "kj_compat.h"

namespace capnp_ls {

static const char* METHOD_NAMES[] = {
#define METHOD_NAME(id, name) name,
    LSP_FOR_EACH_METHOD(METHOD_NAME)
#undef METHOD_NAME
};

kj::StringPtr KJ_STRINGIFY(LspMethod method) {
    return METHOD_NAMES[static_cast<unsigned int>(method)];
}

kj::Maybe<LspMethod> tryParseLspMethod(kj::StringPtr name) {
#define TRY_METHOD(id, methodName) \
    if (name == methodName) { \
        return LspMethod::id; \
    }
    LSP_FOR_EACH_METHOD(TRY_METHOD)
#undef TRY_METHOD

    return CAPNP_LS_NONE;
}

bool containsPosition(const Range &range, uint32_t line, uint32_t character) {
  if (line < range.start.line || line > range.end.line) {
    return false;
  }
  if (line == range.start.line && character < range.start.character) {
    return false;
  }
  if (line == range.end.line && character > range.end.character) {
    return false;
  }
  return true;
}

bool isTighterRange(const Range &candidate, const Range &best) {
  uint32_t candidateLineSpan = candidate.end.line - candidate.start.line;
  uint32_t bestLineSpan = best.end.line - best.start.line;
  if (candidateLineSpan != bestLineSpan) {
    return candidateLineSpan < bestLineSpan;
  }
  int64_t candidateCharSpan = static_cast<int64_t>(candidate.end.character) -
      static_cast<int64_t>(candidate.start.character);
  int64_t bestCharSpan = static_cast<int64_t>(best.end.character) -
      static_cast<int64_t>(best.start.character);
  return candidateCharSpan < bestCharSpan;
}
} // namespace capnp_ls
