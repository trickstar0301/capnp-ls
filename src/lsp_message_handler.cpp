// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_message_handler.h"
#include "json_rpc.h"
#include "keyword_completion.h"
#include "kj_compat.h"
#include "lsp_json.h"
#include "lsp_types.h"
#include "ordinal_completion.h"
#include "workspace_state.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <iostream>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string.h>
#include <unistd.h>

namespace capnp_ls {

namespace {

struct JsonRpcRequest {
  kj::StringPtr method;
  kj::Maybe<double> id;
  capnp::JsonValue::Reader params;
};

JsonRpcRequest parseJsonRpcEnvelope(capnp::JsonValue::Reader root) {
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

  return JsonRpcRequest{method, maybeRequestId, params};
}

} // namespace

LspMessageHandler::LspMessageHandler(
    ServerContext &serverContext,
    StdoutWriter &stdoutWriter)
    : context(serverContext), stdoutWriter(stdoutWriter),
      diagnosticPublisher(stdoutWriter) {
  compilationManager = kj::heap<CompilationManager>();
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

      auto rpcRequest = parseJsonRpcEnvelope(root.asReader());

      auto responseMessageBuilder = kj::heap<capnp::MallocMessageBuilder>();
      kj::Promise<void> promise = kj::READY_NOW;

      CAPNP_LS_IF_SOME (methodEnum, tryParseLspMethod(rpcRequest.method)) {
        promise = dispatch(*methodEnum, rpcRequest.params, *responseMessageBuilder);
      } else {
        KJ_LOG(INFO, "Unsupported method", rpcRequest.method.cStr());
        CAPNP_LS_IF_SOME (requestId, rpcRequest.id) {
          CAPNP_LS_IF_SOME (
              responseString,
              buildErrorResponseString(*requestId, -32601, "Method not found")) {
            (void)stdoutWriter.write(*responseString);
          }
          return kj::READY_NOW;
        }
      }

      CAPNP_LS_IF_SOME (requestId, rpcRequest.id) {
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

kj::Promise<void> LspMessageHandler::compileCapnpFile(kj::StringPtr uri) {
  auto strippedUri = uriToPath(uri);
  if (strippedUri.endsWith(".capnp")) {
    kj::Vector<kj::String> previousDiagnosticFiles;
    for (const auto &[diagnosticUri, _] : index.diagnosticMap) {
      previousDiagnosticFiles.add(kj::heapString(diagnosticUri));
    }

    return compilationManager
        ->compile(CompilationManager::CompileParams(
            workspace.importPaths,
            strippedUri,
            workspace.workspacePath,
            index))
        .then([this,
               strippedUri = kj::mv(strippedUri),
               previousDiagnosticFiles = kj::mv(previousDiagnosticFiles)]() mutable {
          return diagnosticPublisher.publishDiagnostics(
              index.diagnosticMap,
              strippedUri,
              kj::mv(previousDiagnosticFiles));
        });
  }
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::dispatch(LspMethod method, const capnp::JsonValue::Reader &params, capnp::MallocMessageBuilder &response) {
  switch (method) {
  case LspMethod::INITIALIZE:
    return handleInitialize(params, response);
  case LspMethod::SHUTDOWN:
    return handleShutdown();
  case LspMethod::DEFINITION:
    return handleDefinition(params, response);
  case LspMethod::HOVER:
    return handleHover(params, response);
  case LspMethod::REFERENCES:
    return handleReferences(params, response);
  case LspMethod::DOCUMENT_SYMBOL:
    return handleDocumentSymbol(params, response);
  case LspMethod::DID_OPEN:
    return handleDidOpenTextDocument(params);
  case LspMethod::DID_CHANGE:
    return handleDidChangeTextDocument(params);
  case LspMethod::DID_CLOSE:
    return handleDidCloseTextDocument(params);
  case LspMethod::DID_SAVE:
    return handleDidSave(params);
  case LspMethod::DID_CHANGE_WATCHED_FILES:
    return handleDidChangeWatchedFiles(params);
  case LspMethod::COMPLETION:
    return handleCompletion(params, response);
  case LspMethod::FORMATTING:
    return handleFormatting(params, response);
  case LspMethod::INITIALIZED:
  case LspMethod::SET_TRACE:
  case LspMethod::CANCEL_REQUEST:
    break;
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
  return index.findNodeIdAtPosition(path, line, character);
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
      CAPNP_LS_IF_SOME (location, index.nodeLocationMap.find(*id)) {
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
      CAPNP_LS_IF_SOME (metadata, index.nodeMetadataMap.find(*id)) {
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
      CAPNP_LS_IF_SOME (references, index.referenceMap.find(*id)) {
        const Location *declaration = nullptr;
        CAPNP_LS_IF_SOME (declarationLocation, index.nodeLocationMap.find(*id)) {
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
    CAPNP_LS_IF_SOME (symbols, index.documentSymbolMap.find(path)) {
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
                workspace.reloadProjectConfig(index);
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
              workspace.workspacePath = uriToPath(uri);
              KJ_LOG(INFO, "Workspace path set to", workspace.workspacePath);
            }
          }
          }
        }
      } else if (field.getName() == "rootUri" && workspace.workspacePath.size() == 0) {
        if (!field.getValue().isNull()) {
          workspace.workspacePath = uriToPath(field.getValue().getString());
          KJ_LOG(INFO, "Workspace path set from rootUri", workspace.workspacePath);
        }
      } else if (field.getName() == "rootPath" && workspace.workspacePath.size() == 0) {
        if (!field.getValue().isNull()) {
          workspace.workspacePath = kj::heapString(kj::StringPtr(field.getValue().getString()));
          KJ_LOG(INFO, "Workspace path set from rootPath", workspace.workspacePath);
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
                  workspace.importPaths.add(kj::heapString(path.getString()));
                }
                workspace.importPathsConfiguredByInitialization = true;
                KJ_LOG(INFO, "Import paths configured");
              }
            }
          }
        }
      }
    }
    workspace.reloadProjectConfig(index);
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

  auto hoverField = capabilities[2];
  hoverField.setName("hoverProvider");
  hoverField.getValue().setBoolean(true);

  auto referencesField = capabilities[3];
  referencesField.setName("referencesProvider");
  referencesField.getValue().setBoolean(true);

  auto documentSymbolField = capabilities[4];
  documentSymbolField.setName("documentSymbolProvider");
  documentSymbolField.getValue().setBoolean(true);

  auto completionField = capabilities[5];
  completionField.setName("completionProvider");
  auto completionProvider = completionField.getValue().initObject(1);
  completionProvider[0].setName("triggerCharacters");
  auto triggerCharacters = completionProvider[0].getValue().initArray(1);
  triggerCharacters[0].setString("@");

  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDidOpenTextDocument(
    const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling didOpenTextDocument notification");

  try {
    auto paramsObj = params.getObject();
    kj::String uri;
    kj::String text;

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            uri = kj::heapString(docField.getValue().getString());
          } else if (docField.getName() == "text") {
            text = kj::heapString(docField.getValue().getString());
          }
        }
      }
    }
    if (uri.size() > 0) {
      openDocuments.upsert(uriToPath(uri), kj::mv(text));
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

kj::Promise<void> LspMessageHandler::handleDidChangeTextDocument(
    const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling didChangeTextDocument notification");

  try {
    auto paramsObj = params.getObject();
    kj::String uri;
    kj::Maybe<kj::String> fullText;

    for (auto field : paramsObj) {
      if (field.getName() == "textDocument") {
        auto textDocument = field.getValue().getObject();
        for (auto docField : textDocument) {
          if (docField.getName() == "uri") {
            uri = kj::heapString(docField.getValue().getString());
          }
        }
      } else if (field.getName() == "contentChanges") {
        for (auto change : field.getValue().getArray()) {
          // The server advertises full document sync (change = 1), so each
          // change carries the complete text; a range would mean an
          // incremental change we must not apply as a replacement.
          bool hasRange = false;
          kj::Maybe<kj::String> changeText;
          for (auto changeField : change.getObject()) {
            if (changeField.getName() == "range") {
              hasRange = true;
            } else if (changeField.getName() == "text") {
              changeText = kj::heapString(changeField.getValue().getString());
            }
          }
          if (!hasRange) {
            fullText = kj::mv(changeText);
          }
        }
      }
    }
    if (uri.size() > 0) {
      CAPNP_LS_IF_SOME (text, fullText) {
        openDocuments.upsert(uriToPath(uri), kj::mv(*text));
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didChangeTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleDidCloseTextDocument(
    const capnp::JsonValue::Reader &params) {
  KJ_LOG(INFO, "Handling didCloseTextDocument notification");

  try {
    auto path = parseTextDocumentPath(params);
    openDocuments.erase(path);
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didCloseTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}

kj::Promise<void> LspMessageHandler::handleCompletion(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &completionResponseBuilder) {
  KJ_LOG(INFO, "Handling completion request");

  auto root = completionResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName(LSP_RESULT);

  try {
    auto position = parseTextDocumentPosition(params);
    CAPNP_LS_IF_SOME (text, openDocuments.find(position.path)) {
      CAPNP_LS_IF_SOME (
          completion,
          computeOrdinalCompletion(*text, position.line, position.character)) {
        auto label = kj::str(completion->nextOrdinal);
        auto array = resultField.getValue().initArray(1);
        auto item = array[0].initObject(4);
        item[0].setName("label");
        item[0].getValue().setString(label);
        item[1].setName("kind");
        item[1].getValue().setNumber(12); // CompletionItemKind.Value
        item[2].setName("preselect");
        item[2].getValue().setBoolean(true);
        item[3].setName("textEdit");
        auto textEdit = item[3].getValue().initObject(2);
        textEdit[0].setName("range");
        setRange(textEdit[0].getValue(), completion->replaceRange);
        textEdit[1].setName("newText");
        textEdit[1].getValue().setString(label);
        return kj::READY_NOW;
      }
      CAPNP_LS_IF_SOME (
          completion,
          computeKeywordCompletion(*text, position.line, position.character)) {
        auto array = resultField.getValue().initArray(completion->keywords.size());
        for (size_t i = 0; i < completion->keywords.size(); ++i) {
          auto keyword = completion->keywords[i];
          auto item = array[i].initObject(3);
          item[0].setName("label");
          item[0].getValue().setString(keyword);
          item[1].setName("kind");
          item[1].getValue().setNumber(14); // CompletionItemKind.Keyword
          item[2].setName("textEdit");
          auto textEdit = item[2].getValue().initObject(2);
          textEdit[0].setName("range");
          setRange(textEdit[0].getValue(), completion->replaceRange);
          textEdit[1].setName("newText");
          textEdit[1].getValue().setString(keyword);
        }
        return kj::READY_NOW;
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing completion request", e.getDescription());
  }

  resultField.getValue().initArray(0);
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
    // TODO: implement formatting
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didOpenTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}

} // namespace capnp_ls
