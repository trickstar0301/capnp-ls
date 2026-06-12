// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include <capnp/message.h>
#include <kj/filesystem.h>
#include <kj/map.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

struct LinkedCompileResult {
  bool success = false;
  kj::Maybe<kj::Own<capnp::MallocMessageBuilder>> request;
};

class LinkedCompiler {
public:
  static LinkedCompileResult compile(
      kj::StringPtr fileName,
      kj::StringPtr workingDir,
      const kj::Vector<kj::String> &importPaths,
      kj::Filesystem &filesystem,
      kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap);
};

} // namespace capnp_ls
