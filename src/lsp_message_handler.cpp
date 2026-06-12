// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_message_handler.h"
#include "json_rpc.h"
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
namespace {

struct TextDocumentPosition {
  kj::String uri;
  kj::String path;
  uint32_t line = 0;
  uint32_t character = 0;
};

void setPosition(capnp::JsonValue::Builder value, const Position &position) {
  auto obj = value.initObject(2);
  obj[0].setName("line");
  obj[0].getValue().setNumber(position.line - 1);
  obj[1].setName("character");
  obj[1].getValue().setNumber(position.character - 1);
}

void setRange(capnp::JsonValue::Builder value, const Range &range) {
  auto rangeObj = value.initObject(2);
  rangeObj[0].setName("start");
  setPosition(rangeObj[0].getValue(), range.start);
  rangeObj[1].setName("end");
  setPosition(rangeObj[1].getValue(), range.end);
}

void setLocation(capnp::JsonValue::Builder value, const Location &location) {
  auto locationObj = value.initObject(2);
  locationObj[0].setName("uri");
  locationObj[0].getValue().setString(kj::str("file://", location.uri));
  locationObj[1].setName("range");
  setRange(locationObj[1].getValue(), location.range);
}

TextDocumentPosition parseTextDocumentPosition(
    const capnp::JsonValue::Reader &params) {
  auto paramsObj = params.getObject();
  TextDocumentPosition parsed;

  for (auto field : paramsObj) {
    if (field.getName() == "textDocument") {
      auto textDocument = field.getValue().getObject();
      for (auto docField : textDocument) {
        if (docField.getName() == "uri") {
          parsed.uri = kj::heapString(docField.getValue().getString());
          parsed.path = uriToPath(parsed.uri);
        }
      }
    } else if (field.getName() == "position") {
      auto position = field.getValue().getObject();
      for (auto posField : position) {
        if (posField.getName() == "line") {
          parsed.line = posField.getValue().getNumber() + 1;
        } else if (posField.getName() == "character") {
          parsed.character = posField.getValue().getNumber() + 1;
        }
      }
    }
  }

  return parsed;
}

kj::String parseTextDocumentPath(const capnp::JsonValue::Reader &params) {
  auto paramsObj = params.getObject();
  for (auto field : paramsObj) {
    if (field.getName() == "textDocument") {
      auto textDocument = field.getValue().getObject();
      for (auto docField : textDocument) {
        if (docField.getName() == "uri") {
          return uriToPath(docField.getValue().getString());
        }
      }
    }
  }
  return kj::heapString("");
}

} // namespace

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
        case LspMethod::HOVER:
          promise = handleHover(params, *responseMessageBuilder);
          break;
        case LspMethod::REFERENCES:
          promise = handleReferences(params, *responseMessageBuilder);
          break;
        case LspMethod::DOCUMENT_SYMBOL:
          promise = handleDocumentSymbol(params, *responseMessageBuilder);
          break;
        case LspMethod::DID_OPEN:
          promise = handleDidOpenTextDocument(params);
          break;
        case LspMethod::DID_SAVE:
          promise = handleDidSave(params);
          break;
        case LspMethod::DID_CHANGE_WATCHED_FILES:
          promise = handleDidChangeWatchedFiles(params);
          break;
        case LspMethod::FORMATTING:
          promise = handleFormatting(params, *responseMessageBuilder);
          break;
        case LspMethod::INITIALIZED:
        case LspMethod::SET_TRACE:
        case LspMethod::CANCEL_REQUEST:
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
  nodeMetadataMap.clear();
  referenceMap.clear();
  documentSymbolMap.clear();
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
    CAPNP_LS_IF_SOME (logLevel, loadedConfig.logLevel) {
      currentLogLevel = *logLevel;
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
            nodeMetadataMap,
            referenceMap,
            documentSymbolMap,
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

kj::Maybe<uint64_t> LspMessageHandler::findNodeIdAtPosition(
    kj::StringPtr path,
    uint32_t line,
    uint32_t character) {
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

kj::Promise<void> LspMessageHandler::handleDefinition(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &definitionResponseBuilder) {
  KJ_LOG(INFO, "Handling definition request");

  auto root = definitionResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto position = parseTextDocumentPosition(params);
    auto maybeId =
        findNodeIdAtPosition(position.path, position.line, position.character);

    CAPNP_LS_IF_SOME (id, maybeId) {
      CAPNP_LS_IF_SOME (location, nodeLocationMap.find(*id)) {
        setLocation(resultField.getValue(), **location);
        return kj::READY_NOW;
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing definition request", e.getDescription());
  }

  resultField.getValue().setNull();
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleHover(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &hoverResponseBuilder) {
  KJ_LOG(INFO, "Handling hover request");

  auto root = hoverResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto position = parseTextDocumentPosition(params);
    auto maybeId =
        findNodeIdAtPosition(position.path, position.line, position.character);

    CAPNP_LS_IF_SOME (id, maybeId) {
      CAPNP_LS_IF_SOME (metadata, nodeMetadataMap.find(*id)) {
        auto hoverObj = resultField.getValue().initObject(1);
        hoverObj[0].setName("contents");
        auto contents = hoverObj[0].getValue().initObject(2);
        contents[0].setName("kind");
        contents[0].getValue().setString("markdown");
        contents[1].setName("value");
        auto value = (*metadata)->documentation.size() > 0
            ? kj::str(
                  "```capnp\n",
                  (*metadata)->detail,
                  " ",
                  (*metadata)->name,
                  "\n```\n",
                  (*metadata)->documentation)
            : kj::str(
                  "```capnp\n",
                  (*metadata)->detail,
                  " ",
                  (*metadata)->name,
                  "\n```");
        contents[1].getValue().setString(value);
        return kj::READY_NOW;
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing hover request", e.getDescription());
  }

  resultField.getValue().setNull();
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleReferences(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &referencesResponseBuilder) {
  KJ_LOG(INFO, "Handling references request");

  auto root = referencesResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto position = parseTextDocumentPosition(params);
    bool includeDeclaration = true;
    for (auto field : params.getObject()) {
      if (field.getName() == "context") {
        for (auto contextField : field.getValue().getObject()) {
          if (contextField.getName() == "includeDeclaration") {
            includeDeclaration = contextField.getValue().getBoolean();
          }
        }
      }
    }

    auto maybeId =
        findNodeIdAtPosition(position.path, position.line, position.character);

    CAPNP_LS_IF_SOME (id, maybeId) {
      CAPNP_LS_IF_SOME (references, referenceMap.find(*id)) {
        const Location *declaration = nullptr;
        CAPNP_LS_IF_SOME (declarationLocation, nodeLocationMap.find(*id)) {
          declaration = declarationLocation->get();
        }

        size_t resultSize = 0;
        for (const auto &reference : *references) {
          bool isDeclaration = declaration != nullptr &&
              declaration->uri == reference.uri &&
              declaration->range == reference.range;
          if (includeDeclaration || !isDeclaration) {
            ++resultSize;
          }
        }

        auto array = resultField.getValue().initArray(resultSize);
        size_t index = 0;
        for (const auto &reference : *references) {
          bool isDeclaration = declaration != nullptr &&
              declaration->uri == reference.uri &&
              declaration->range == reference.range;
          if (includeDeclaration || !isDeclaration) {
            setLocation(array[index], reference);
            ++index;
          }
        }
        return kj::READY_NOW;
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing references request", e.getDescription());
  }

  resultField.getValue().initArray(0);
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDocumentSymbol(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &documentSymbolResponseBuilder) {
  KJ_LOG(INFO, "Handling documentSymbol request");

  auto root = documentSymbolResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto path = parseTextDocumentPath(params);
    CAPNP_LS_IF_SOME (symbols, documentSymbolMap.find(path)) {
      auto array = resultField.getValue().initArray(symbols->size());
      for (size_t i = 0; i < symbols->size(); ++i) {
        const auto &symbol = (*symbols)[i];
        auto symbolObj = array[i].initObject(5);
        symbolObj[0].setName("name");
        symbolObj[0].getValue().setString(symbol.name);
        symbolObj[1].setName("detail");
        symbolObj[1].getValue().setString(symbol.detail);
        symbolObj[2].setName("kind");
        symbolObj[2].getValue().setNumber(symbol.kind);
        symbolObj[3].setName("range");
        setRange(symbolObj[3].getValue(), symbol.range);
        symbolObj[4].setName("selectionRange");
        setRange(symbolObj[4].getValue(), symbol.selectionRange);
      }
      return kj::READY_NOW;
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing documentSymbol request", e.getDescription());
  }

  resultField.getValue().initArray(0);
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

  auto capabilities = capsField.getValue().initObject(6);

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

  auto hoverField = capabilities[3];
  hoverField.setName("hoverProvider");
  hoverField.getValue().setBoolean(true);

  auto referencesField = capabilities[4];
  referencesField.setName("referencesProvider");
  referencesField.getValue().setBoolean(true);

  auto documentSymbolField = capabilities[5];
  documentSymbolField.setName("documentSymbolProvider");
  documentSymbolField.getValue().setBoolean(true);

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
