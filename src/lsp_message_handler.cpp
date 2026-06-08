// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_message_handler.h"
#include "kj_compat.h"
#include "lsp_types.h"
#include "project_config.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <iostream>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string.h>
#include <unistd.h>

namespace capnp_ls {

LspMessageHandler::LspMessageHandler(
    ServerContext &serverContext,
    StdoutWriter &stdoutWriter)
    : context(serverContext), stdoutWriter(stdoutWriter) {
  compilationManager = kj::heap<CompilationManager>(context.getIoContext());
}

kj::Promise<void>
LspMessageHandler::handleMessage(kj::Maybe<kj::String> maybeMessage) {
  try {
    CAPNP_LS_IF_SOME (message, maybeMessage) {
      const char *headerEnd = strstr(message->begin(), LSP_HEADER_DELIMITER);
      if (!headerEnd) {
        KJ_LOG(ERROR, "Invalid message format: no header delimiter found");
        (void)handleShutdown();
        return kj::READY_NOW;
      }

      const char *jsonStart = headerEnd + LSP_HEADER_DELIMITER_SIZE;
      size_t jsonLength = message->end() - jsonStart;

      capnp::JsonCodec codec;
      kj::ArrayPtr<const char> jsonContent(jsonStart, jsonLength);

      capnp::MallocMessageBuilder messageBuilder;
      auto root = messageBuilder.initRoot<capnp::JsonValue>();

      codec.decodeRaw(jsonContent, root);

      auto obj = root.getObject();
      kj::StringPtr method;
      kj::Maybe<double> maybeRequestId;
      capnp::JsonValue::Reader params;

      for (auto field : obj) {
        kj::StringPtr name = field.getName();
        if (name == LSP_METHOD) {
          method = field.getValue().getString();
        } else if (name == LSP_ID) {
          if (field.getValue().isNumber()) {
            maybeRequestId = field.getValue().getNumber();
          } else if (field.getValue().isNull()) {
          } else {
            KJ_LOG(ERROR, "Invalid ID type", field.getValue().which());
          }
        } else if (name == LSP_PARAMS) {
          params = field.getValue();
        }
      }

      auto responseMessageBuilder = kj::heap<capnp::MallocMessageBuilder>();
      kj::Promise<void> promise = kj::READY_NOW;

      CAPNP_LS_IF_SOME (methodEnum, tryParseLspMethod(method)) {
        switch (*methodEnum) {
        case LspMethod::INITIALIZE:
          promise = handleInitialize(params, *responseMessageBuilder);
          break;
        case LspMethod::SHUTDOWN:
          promise = handleShutdown();
          break;
        case LspMethod::DEFINITION:
          promise = handleDefinition(params, *responseMessageBuilder);
          break;
        case LspMethod::DID_OPEN:
          promise = handleDidOpenTextDocument(params);
          break;
        case LspMethod::DID_SAVE:
          promise = handleDidSave(params);
          break;
        case LspMethod::FORMATTING:
          promise = handleFormatting(params, *responseMessageBuilder);
          break;
        case LspMethod::INITIALIZED:
        case LspMethod::SET_TRACE:
        case LspMethod::CANCEL_REQUEST:
        case LspMethod::DID_CHANGE_WATCHED_FILES:
        case LspMethod::DID_CHANGE:
        case LspMethod::DID_CLOSE:
          // KJ_LOG(INFO, "Ignoring method", method.cStr());
          break;
        }
      } else {
        KJ_LOG(INFO, "Unsupported method", method.cStr());
        CAPNP_LS_IF_SOME (requestId, maybeRequestId) {
          CAPNP_LS_IF_SOME (
              responseString,
              buildErrorResponseString(*requestId, -32601, "Method not found")) {
            (void)stdoutWriter.write(*responseString);
          }
          return kj::READY_NOW;
        }
      }

      CAPNP_LS_IF_SOME (requestId, maybeRequestId) {
        return promise.then(
            [this,
             id = *requestId,
             builder = kj::mv(responseMessageBuilder)]() mutable {
              auto response = builder->getRoot<capnp::JsonValue>().asReader();
              CAPNP_LS_IF_SOME (responseString, buildResponseString(id, response)) {
                (void)stdoutWriter.write(*responseString);
              }
              return kj::Promise<void>(kj::READY_NOW);
            });
      } else {
        return promise.then([]() { return kj::Promise<void>(kj::READY_NOW); });
      }

    } else {
      KJ_LOG(INFO, "EOF detected on stdin, initiating shutdown...");
      (void)handleShutdown();
    }
  } catch (const std::exception &e) {
    KJ_LOG(ERROR, "Error processing message", e.what());
  }
  return kj::Promise<void>(kj::READY_NOW);
}

void LspMessageHandler::clearCompilationState() {
  fileSourceInfoMap.clear();
  nodeLocationMap.clear();
  diagnosticMap.clear();
}

bool LspMessageHandler::isProjectConfigPath(kj::StringPtr path) {
  return path.endsWith(kj::str("/", PROJECT_CONFIG_FILE)) ||
      path == PROJECT_CONFIG_FILE;
}

bool LspMessageHandler::reloadProjectConfig() {
  if (workspacePath.size() == 0) {
    KJ_LOG(INFO, "Skipping project config reload because workspace path is not set");
    return false;
  }

  ProjectConfig loadedConfig;
  if (loadProjectConfig(workspacePath, loadedConfig)) {
    KJ_IF_SOME(logLevel, loadedConfig.logLevel) {
      currentLogLevel = logLevel;
    } else {
      currentLogLevel = defaultLogLevel;
    }
    applyLogLevel(currentLogLevel);

    if (!importPathsConfiguredByInitialization) {
      importPaths.clear();
      for (auto &path : loadedConfig.importPaths) {
        importPaths.add(kj::mv(path));
      }
    } else {
      KJ_LOG(INFO, "Skipping project config import paths because initialization options configured import paths");
    }

    clearCompilationState();
    KJ_LOG(INFO, "Project config reloaded");
    return true;
  }

  if (!projectConfigExists(workspacePath)) {
    currentLogLevel = defaultLogLevel;
    applyLogLevel(currentLogLevel);
    if (!importPathsConfiguredByInitialization) {
      importPaths.clear();
    }
    clearCompilationState();
    KJ_LOG(INFO, "Project config removed; defaults restored");
    return true;
  }

  KJ_LOG(ERROR, "Keeping previous project config because reload failed");
  return false;
}

kj::Maybe<kj::String> LspMessageHandler::buildResponseString(
    const double id,
    const capnp::JsonValue::Reader &result) {
  try {
    capnp::MallocMessageBuilder messageBuilder;
    auto root = messageBuilder.initRoot<capnp::JsonValue>();
    auto obj = root.initObject(3);

    obj[0].setName(LSP_JSONRPC);
    obj[0].getValue().setString(LSP_JSON_RPC_VERSION);

    obj[1].setName(LSP_ID);
    obj[1].getValue().setNumber(id);

    obj[2].setName(LSP_RESULT);
    if (result.isObject() && result.getObject().size() > 0 &&
        result.getObject()[0].getValue().isObject()) {
      obj[2].getValue().setObject(result.getObject()[0].getValue().getObject());
    } else {
      obj[2].getValue().setNull();
    }

    capnp::JsonCodec codec;
    kj::String responseStr =
        codec.encodeRaw(messageBuilder.getRoot<capnp::JsonValue>());
    // KJ_LOG(INFO, "Encoded response", responseStr.cStr());

    return kj::str(
        LSP_CONTENT_LENGTH_HEADER,
        responseStr.size(),
        LSP_HEADER_DELIMITER,
        responseStr);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error building response string", e.getDescription());
    return CAPNP_LS_NONE;
  }
}

kj::Maybe<kj::String> LspMessageHandler::buildErrorResponseString(
    const double id,
    int code,
    kj::StringPtr message) {
  try {
    capnp::MallocMessageBuilder messageBuilder;
    auto root = messageBuilder.initRoot<capnp::JsonValue>();
    auto obj = root.initObject(3);

    obj[0].setName(LSP_JSONRPC);
    obj[0].getValue().setString(LSP_JSON_RPC_VERSION);

    obj[1].setName(LSP_ID);
    obj[1].getValue().setNumber(id);

    obj[2].setName(LSP_ERROR);
    auto error = obj[2].getValue().initObject(2);

    error[0].setName("code");
    error[0].getValue().setNumber(code);

    error[1].setName("message");
    error[1].getValue().setString(message);

    capnp::JsonCodec codec;
    kj::String responseStr =
        codec.encodeRaw(messageBuilder.getRoot<capnp::JsonValue>());

    return kj::str(
        LSP_CONTENT_LENGTH_HEADER,
        responseStr.size(),
        LSP_HEADER_DELIMITER,
        responseStr);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error building error response string", e.getDescription());
    return CAPNP_LS_NONE;
  }
}

kj::Promise<void> LspMessageHandler::compileCapnpFile(kj::StringPtr uri) {
  auto strippedUri = uriToPath(uri);
  if (strippedUri.endsWith(".capnp")) {
    kj::Vector<kj::String> previousDiagnosticFiles;
    for (const auto &[diagnosticUri, _] : diagnosticMap) {
      previousDiagnosticFiles.add(kj::heapString(diagnosticUri));
    }

    return compilationManager
        ->compile(CompilationManager::CompileParams(
            importPaths,
            strippedUri,
            workspacePath,
            fileSourceInfoMap,
            nodeLocationMap,
            diagnosticMap))
        .then([this,
               strippedUri = kj::mv(strippedUri),
               previousDiagnosticFiles = kj::mv(previousDiagnosticFiles)]() mutable {
          return publishDiagnostics(
              strippedUri,
              kj::mv(previousDiagnosticFiles));
        });
  }
  return kj::READY_NOW;
}

kj::Promise<void>
LspMessageHandler::publishDiagnostics(
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
    CAPNP_LS_IF_SOME (_, diagnosticMap.find(previousFile)) {
      stillHasDiagnostics = true;
    }
    if (previousFile != fileName && !stillHasDiagnostics) {
      (void)publishDiagnosticsForFile(previousFile, nullptr);
    }
  }

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::publishDiagnosticsForFile(
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
    kj::String message = kj::str(
        LSP_CONTENT_LENGTH_HEADER,
        notificationStr.size(),
        LSP_HEADER_DELIMITER,
        notificationStr);

    (void)stdoutWriter.write(message);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error publishing diagnostics", e.getDescription());
  }

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleShutdown() {
  KJ_LOG(INFO, "Handling shutdown request");
  context.shutdown();
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDefinition(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &definitionResponseBuilder) {
  KJ_LOG(INFO, "Handling definition request");

  auto root = definitionResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto paramsObj = params.getObject();
    kj::String uri;
    uint32_t line = 0;
    uint32_t character = 0;

    KJ_LOG(INFO, "Parsing parameters");

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            uri = kj::heapString(docField.getValue().getString());
            KJ_LOG(INFO, "Found URI", uri);
          }
        }
      } else if (field.getName() == "position") {
        auto position = field.getValue().getObject();
        for (auto posField : position) {
          if (posField.getName() == "line") {
            line = posField.getValue().getNumber() + 1;
            KJ_LOG(INFO, "Found", line);
          } else if (posField.getName() == "character") {
            character = posField.getValue().getNumber() + 1;
            KJ_LOG(INFO, "Found", character);
          }
        }
      }
    }

    // erase file:// prefix and workspacePath from uri
    kj::String strippedUri = uriToPath(uri);

    KJ_LOG(
        INFO,
        "Definition request params:",
        strippedUri.cStr(),
        line,
        character);

    CAPNP_LS_IF_SOME (rangeMap, fileSourceInfoMap.find(strippedUri)) {
      for (const auto &[range, id] : *rangeMap) {
        if (range.start.line <= line && line <= range.end.line &&
            range.start.character <= character &&
            character <= range.end.character) {

          KJ_LOG(INFO, "Found range for ", id);

          CAPNP_LS_IF_SOME (location, nodeLocationMap.find(id)) {
            KJ_LOG(INFO, "Found location");

            auto locationObj = resultField.getValue().initObject(2);

            // Uri
            auto uriField = locationObj[0];
            uriField.setName("uri");
            kj::String fullUri = kj::str("file://", (*location)->uri);
            uriField.getValue().setString(fullUri);

            // Range
            auto rangeField = locationObj[1];
            rangeField.setName("range");
            auto rangeObj = rangeField.getValue().initObject(2);

            // Start position
            auto startField = rangeObj[0];
            startField.setName("start");
            auto startObj = startField.getValue().initObject(2);
            startObj[0].setName("line");
            startObj[0].getValue().setNumber((*location)->range.start.line - 1);
            startObj[1].setName("character");
            startObj[1].getValue().setNumber(
                (*location)->range.start.character - 1);

            auto endField = rangeObj[1];
            endField.setName("end");
            auto endObj = endField.getValue().initObject(2);
            endObj[0].setName("line");
            endObj[0].getValue().setNumber((*location)->range.end.line - 1);
            endObj[1].setName("character");
            endObj[1].getValue().setNumber(
                (*location)->range.end.character - 1);

            KJ_LOG(INFO, "Response structure complete");
            return kj::READY_NOW;
          }
        }
      }
    } else {
      KJ_LOG(
          INFO,
          kj::str(
              "SourceInfo not found for ",
              strippedUri,
              ". The file may still be compiling or compilation may have failed."));
    }

    resultField.getValue().setNull();
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing definition request", e.getDescription());
    resultField.getValue().setNull();
  }

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDidChangeWatchedFiles(
    const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling onDidChangeWatchedFiles notification");
  KJ_LOG(INFO, "params", params);
  try {
    auto paramsObj = params.getObject();

    for (auto field : paramsObj) {
      if (field.getName() == "changes") {
        auto changes = field.getValue().getArray();
        for (auto change : changes) {
          auto changeObj = change.getObject();
          for (auto changeField : changeObj) {
            if (changeField.getName() == "uri") {
              auto uri = kj::heapString(changeField.getValue().getString());
              KJ_LOG(INFO, "URI", uri.cStr());

              auto path = uriToPath(uri);
              if (isProjectConfigPath(path)) {
                reloadProjectConfig();
                return kj::READY_NOW;
              }

              return compileCapnpFile(uri);
            }
          }
        }
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didChangeWatchedFiles notification",
        e.getDescription());
  }

  return kj::READY_NOW;
}

kj::Promise<void>
LspMessageHandler::handleDidSave(const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling onDidSave notification");
  KJ_LOG(INFO, "params", params);
  try {
    auto paramsObj = params.getObject();

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            auto uri = kj::heapString(docField.getValue().getString());
            KJ_LOG(INFO, "URI", uri.cStr());
            return compileCapnpFile(uri);
          }
        }
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing didSave notification", e.getDescription());
  }

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleInitialize(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &initializeResponseBuilder) {
  KJ_LOG(INFO, "Handling initialize request");

  try {
    auto paramsObj = params.getObject();
    for (auto field : paramsObj) {
      if (field.getName() == "workspaceFolders") {
        if (field.getValue().isArray()) {
          auto folders = field.getValue().getArray();
          if (folders.size() > 0) {
          auto firstFolder = folders[0].getObject();
          for (auto folderField : firstFolder) {
            if (folderField.getName() == "uri") {
              auto uri = kj::heapString(folderField.getValue().getString());
              workspacePath = uriToPath(uri);
              KJ_LOG(INFO, "Workspace path set to", workspacePath);
            }
          }
          }
        }
      } else if (field.getName() == "rootUri" && workspacePath.size() == 0) {
        if (!field.getValue().isNull()) {
          workspacePath = uriToPath(field.getValue().getString());
          KJ_LOG(INFO, "Workspace path set from rootUri", workspacePath);
        }
      } else if (field.getName() == "rootPath" && workspacePath.size() == 0) {
        if (!field.getValue().isNull()) {
          workspacePath = kj::heapString(kj::StringPtr(field.getValue().getString()));
          KJ_LOG(INFO, "Workspace path set from rootPath", workspacePath);
        }
      } else if (field.getName() == "initializationOptions") {
        auto initOptions = field.getValue().getObject();
        for (auto optField : initOptions) {
          if (optField.getName() == "capnp") {
            auto capnpConfig = optField.getValue().getObject();
            for (auto configField : capnpConfig) {
              if (configField.getName() == "importPaths") {
                auto paths = configField.getValue().getArray();
                for (auto path : paths) {
                  importPaths.add(kj::heapString(path.getString()));
                }
                importPathsConfiguredByInitialization = true;
                KJ_LOG(INFO, "Import paths configured");
              }
            }
          }
        }
      }
    }
    reloadProjectConfig();
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing initialize params", e.getDescription());
  }

  auto root = initializeResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName("result");

  auto resultValue = resultField.getValue().initObject(1);
  auto capsField = resultValue[0];
  capsField.setName("capabilities");

  auto capabilities = capsField.getValue().initObject(3);

  // Set text document sync capability
  auto syncField = capabilities[0];
  syncField.setName("textDocumentSync");
  auto syncObj = syncField.getValue().initObject(3);

  auto openCloseField = syncObj[0];
  openCloseField.setName("openClose");
  openCloseField.getValue().setBoolean(true);

  auto changeField = syncObj[1];
  changeField.setName("change");
  changeField.getValue().setNumber(1);

  auto saveField = syncObj[2];
  saveField.setName("save");
  saveField.getValue().setBoolean(true);

  // Set definition provider capability
  auto defField = capabilities[1];
  defField.setName("definitionProvider");
  defField.getValue().setBoolean(true);

  // Set workspace/didChangeWatchedFiles capability
  auto watchedFilesField = capabilities[2];
  watchedFilesField.setName("workspace/didChangeWatchedFiles");
  watchedFilesField.getValue().setBoolean(true);

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDidOpenTextDocument(
    const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling didOpenTextDocument notification");

  try {
    auto paramsObj = params.getObject();
    kj::String uri;

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            uri = kj::heapString(docField.getValue().getString());
          }
        }
      }
    }
    return compileCapnpFile(uri);
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didOpenTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleFormatting(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &formattingResponseBuilder) {
  try {
    auto paramsObj = params.getObject();
    kj::String uri;

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            uri = kj::heapString(docField.getValue().getString());
          }
        }
      }
    }
    KJ_LOG(ERROR, "Formatting capability is not implemented yet");
    // TODO: Call CompilationManager::format
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didOpenTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}

} // namespace capnp_ls
