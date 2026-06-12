// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "diagnostic_publisher.h"
#include "json_rpc.h"
#include "kj_compat.h"
#include "lsp_json.h"
#include "lsp_types.h"
#include "utils.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <kj/debug.h>

namespace capnp_ls {

DiagnosticPublisher::DiagnosticPublisher(StdoutWriter &writer)
    : stdoutWriter(writer) {}

kj::Promise<void> DiagnosticPublisher::publishDiagnostics(
    const kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap,
    kj::StringPtr fileName,
    kj::Vector<kj::String> previousDiagnosticFiles) {
  KJ_LOG(INFO, "Publishing diagnostics");

  bool publishedCurrentFile = false;
  for (const auto &[uri, diagnostics] : diagnosticMap) {
    if (uri == fileName) {
      publishedCurrentFile = true;
    }
    (void)publishDiagnosticsForFile(uri, &diagnostics);
  }

  if (!publishedCurrentFile) {
    (void)publishDiagnosticsForFile(fileName, nullptr);
  }

  for (auto &previousFile : previousDiagnosticFiles) {
    bool stillHasDiagnostics = false;
    CAPNP_LS_IF_SOME (diagnostics, diagnosticMap.find(previousFile)) {
      (void)diagnostics;
      stillHasDiagnostics = true;
    }
    if (previousFile != fileName && !stillHasDiagnostics) {
      (void)publishDiagnosticsForFile(previousFile, nullptr);
    }
  }

  return kj::READY_NOW;
}

kj::Promise<void> DiagnosticPublisher::publishDiagnosticsForFile(
    kj::StringPtr fileName,
    const kj::Vector<Diagnostic> *diagnostics) {
  try {
    capnp::MallocMessageBuilder messageBuilder;
    auto root = messageBuilder.initRoot<capnp::JsonValue>();
    auto notificationObj = root.initObject(3);

    // Set jsonrpc version
    notificationObj[0].setName(LSP_JSONRPC);
    notificationObj[0].getValue().setString(LSP_JSON_RPC_VERSION);

    // Set method
    notificationObj[1].setName(LSP_METHOD);
    notificationObj[1].getValue().setString("textDocument/publishDiagnostics");

    // Set params
    notificationObj[2].setName(LSP_PARAMS);
    auto params = notificationObj[2].getValue().initObject(2);

    // Set URI
    params[0].setName("uri");
    kj::String fullUri = pathToUri(fileName);
    params[0].getValue().setString(fullUri);

    // Set diagnostics array
    params[1].setName("diagnostics");
    auto diagnosticsSize = diagnostics == nullptr ? 0 : diagnostics->size();
    auto diagnosticsArray = params[1].getValue().initArray(diagnosticsSize);

    for (size_t i = 0; i < diagnosticsSize; i++) {
      const auto &diagnostic = (*diagnostics)[i];
      auto diagnosticObj = diagnosticsArray[i].initObject(3);

      // Set severity
      diagnosticObj[0].setName("severity");
      diagnosticObj[0].getValue().setNumber(static_cast<double>(diagnostic.severity));

      // Set message
      diagnosticObj[1].setName("message");
      diagnosticObj[1].getValue().setString(diagnostic.message);

      // Set range
      diagnosticObj[2].setName("range");
      setRange(diagnosticObj[2].getValue(), diagnostic.range);
    }

    // Encode and send the notification
    capnp::JsonCodec codec;
    kj::String notificationStr = codec.encodeRaw(root);
    kj::String message = frameLspMessage(notificationStr);

    (void)stdoutWriter.write(message);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error publishing diagnostics", e.getDescription());
  }

  return kj::READY_NOW;
}

} // namespace capnp_ls
