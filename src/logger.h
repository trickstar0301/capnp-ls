// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "stdout_writer.h"
#include <capnp/compat/json.h>
#include <cstdlib>
#include <kj/debug.h>
#include <kj/exception.h>

namespace capnp_ls {

enum class LogLevel { ERROR, WARNING, INFO };

class LspLogger : public kj::ExceptionCallback {
public:
  LspLogger(StdoutWriter &writer) : writer(writer) {
    // Get log level from environment variable
    const char *logEnv = std::getenv("CPP_LOG");
    LogLevel level = LogLevel::WARNING; // Default log level

    if (logEnv != nullptr) {
      kj::StringPtr logStr(logEnv);

      // Parse environment variable format: lsp_server=level
      if (logStr.startsWith("lsp_server=")) {
        auto levelStr = logStr.slice(11); // Skip "lsp_server="

        if (levelStr == "error") {
          level = LogLevel::ERROR;
        } else if (levelStr == "warning") {
          level = LogLevel::WARNING;
        } else if (levelStr == "info") {
          level = LogLevel::INFO;
        }
      }
    }

    // Set KJ log level
    kj::LogSeverity kjLogLevel;
    switch (level) {
    case LogLevel::ERROR:
      kjLogLevel = kj::LogSeverity::ERROR;
      break;
    case LogLevel::WARNING:
      kjLogLevel = kj::LogSeverity::WARNING;
      break;
    case LogLevel::INFO:
      kjLogLevel = kj::LogSeverity::INFO;
      break;
    }
    kj::_::Debug::setLogLevel(kjLogLevel);

    // Log the current log level
    KJ_LOG(INFO, "Log level set to", kjLogLevel);
  }

  void logMessage(
      kj::LogSeverity severity,
      const char *file,
      int line,
      int,
      kj::String &&text) override {
    // Include file path and line number in the log message
    kj::String fullMessage = kj::str(file, ":", line, ": ", text);

    // Send to LSP client
    sendLspLogMessage(severity, kj::mv(fullMessage));
  }

private:
  kj::Promise<void>
  sendLspLogMessage(kj::LogSeverity severity, kj::String text) {
    // Convert KJ severity to LSP MessageType
    // 1 = Error, 2 = Warning, 3 = Info, 4 = Log
    int messageType;
    bool isFatal = false;
    switch (severity) {
    case kj::LogSeverity::FATAL:
      messageType = 1;
      isFatal = true;
      break;
    case kj::LogSeverity::ERROR:
      messageType = 1;
      break;
    case kj::LogSeverity::WARNING:
      messageType = 2;
      break;
    case kj::LogSeverity::INFO:
      messageType = 3;
      break;
    default:
      messageType = 4;
    }

    // Build LSP notification
    capnp::MallocMessageBuilder messageBuilder;
    auto root = messageBuilder.initRoot<capnp::JsonValue>();
    auto notificationObj = root.initObject(3);

    notificationObj[0].setName("jsonrpc");
    notificationObj[0].getValue().setString("2.0");

    notificationObj[1].setName("method");
    if (isFatal) {
      notificationObj[1].getValue().setString("window/showMessage");
    } else {
      notificationObj[1].getValue().setString("window/logMessage");
    }

    notificationObj[2].setName("params");
    auto params = notificationObj[2].getValue().initObject(2);

    params[0].setName("type");
    params[0].getValue().setNumber(messageType);

    params[1].setName("message");
    params[1].getValue().setString(text);

    capnp::JsonCodec codec;
    kj::String jsonStr = codec.encodeRaw(root);

    kj::String message =
        kj::str("Content-Length: ", jsonStr.size(), "\r\n\r\n", jsonStr);

    return writer.write(message);
  }

  StdoutWriter &writer;
};

} // namespace capnp_ls