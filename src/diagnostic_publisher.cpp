// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "diagnostic_publisher.h"
#include "json_rpc.h"
#include "kj_compat.h"
#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <kj/debug.h>

namespace capnp_ls {

DiagnosticPublisher::DiagnosticPublisher(StdoutWriter &writer)
    : stdoutWriter(writer) {}

kj::Promise<void> DiagnosticPublisher::publishDiagnostics(
    const kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap,
    kj::StringPtr fileName,
    kj::Vector<kj::String> previousDiagnosticFiles,
    kj::StringPtr workspacePath) {
  KJ_LOG(INFO, "Publishing diagnostics");

  bool publishedCurrentFile = false;
  for (const auto &[uri, diagnostics] : diagnosticMap) {
    if (uri == fileName) {
      publishedCurrentFile = true;
    }
    (void)publishDiagnosticsForFile(uri, &diagnostics, workspacePath);
  }

  if (!publishedCurrentFile) {
    (void)publishDiagnosticsForFile(fileName, nullptr, workspacePath);
  }

  for (auto &previousFile : previousDiagnosticFiles) {
    bool stillHasDiagnostics = false;
    CAPNP_LS_IF_SOME (diagnostics, diagnosticMap.find(previousFile)) {
      (void)diagnostics;
      stillHasDiagnostics = true;
    }
    if (previousFile != fileName && !stillHasDiagnostics) {
      (void)publishDiagnosticsForFile(previousFile, nullptr, workspacePath);
    }
  }

  return kj::READY_NOW;
}

kj::Promise<void> DiagnosticPublisher::publishDiagnosticsForFile(
    kj::StringPtr fileName,
    const kj::Vector<Diagnostic> *diagnostics,
    kj::StringPtr workspacePath) {
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
    // Ensure fileName is relative to workspacePath
    kj::StringPtr relativeFileName = fileName;
    if (fileName.startsWith(workspacePath)) {
      relativeFileName = fileName.slice(
          workspacePath.size() + 1); // +1 for the trailing slash
    }
    kj::String fullUri =
        kj::str("file://", workspacePath, "/", relativeFileName);
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
      diagnosticObj[0].getValue().setNumber(1); // Error = 1

      // Set message
      diagnosticObj[1].setName("message");
      diagnosticObj[1].getValue().setString(diagnostic.message);

      // Set range
      diagnosticObj[2].setName("range");
      auto rangeObj = diagnosticObj[2].getValue().initObject(2);

      // Start position
      auto startObj = rangeObj[0];
      startObj.setName("start");
      auto start = startObj.getValue().initObject(2);
      start[0].setName("line");
      start[0].getValue().setNumber(diagnostic.range.start.line);
      start[1].setName("character");
      start[1].getValue().setNumber(diagnostic.range.start.character);

      // End position
      auto endObj = rangeObj[1];
      endObj.setName("end");
      auto end = endObj.getValue().initObject(2);
      end[0].setName("line");
      end[0].getValue().setNumber(diagnostic.range.end.line);
      end[1].setName("character");
      end[1].getValue().setNumber(diagnostic.range.end.character);
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
