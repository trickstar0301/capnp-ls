// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_types.h"

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
    
    return nullptr;
}
} // namespace capnp_ls