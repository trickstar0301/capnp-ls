// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <capnp/message.h>
#include <kj/async-io.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

class CompileErrorParser {
public:
  // Returns 0 on success, non-zero on failure
  static int parse(
      kj::StringPtr fileName,
      kj::StringPtr errorText,
      kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap);

private:
  static void addDiagnostic(
      const kj::String &file,
      uint32_t rowStart,
      uint32_t rowEnd,
      uint32_t colStart,
      uint32_t colEnd,
      const kj::String &type,
      const kj::String &message,
      kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap);
};

} // namespace capnp_ls
