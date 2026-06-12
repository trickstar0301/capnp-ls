// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_types.h"
#include "stdout_writer.h"
#include <kj/async.h>
#include <kj/map.h>
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

class DiagnosticPublisher {
public:
  explicit DiagnosticPublisher(StdoutWriter &writer);
  kj::Promise<void> publishDiagnostics(
      const kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap,
      kj::StringPtr fileName,
      kj::Vector<kj::String> previousDiagnosticFiles,
      kj::StringPtr workspacePath);
  kj::Promise<void> publishDiagnosticsForFile(
      kj::StringPtr fileName,
      const kj::Vector<Diagnostic> *diagnostics,
      kj::StringPtr workspacePath);

private:
  StdoutWriter &stdoutWriter;
};

} // namespace capnp_ls
