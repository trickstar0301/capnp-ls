// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "capnp_language_server.h"

namespace capnp_ls {

CapnpLanguageServer::CapnpLanguageServer(ServerContext &serverContext)
    : context(serverContext) {
  compilationManager = kj::heap<CompilationManager>(context.getIoContext());
}

kj::Promise<void> CapnpLanguageServer::compileCapnpFile(kj::StringPtr uri) {
  auto strippedUri = uriToPath(uri);
  if (strippedUri.endsWith(".capnp")) {
    return compilationManager->compile(CompilationManager::CompileParams{
        .compilerPath = compilerPath,
        .importPaths = importPaths,
        .fileName = strippedUri,
        .workingDir = workspacePath,
        .fileSourceInfoMap = fileSourceInfoMap,
        .nodeLocationMap = nodeLocationMap});
  }
  return kj::READY_NOW;
}

kj::Promise<void> CapnpLanguageServer::onShutdown() {
  KJ_LOG(INFO, "Handling shutdown request");
  context.shutdown();
  return kj::READY_NOW;
}

kj::Promise<void> CapnpLanguageServer::onDefinition(
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
      KJ_LOG(INFO, "Processing field", field.getName());
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
    if (strippedUri.startsWith(workspacePath)) {
      strippedUri = kj::heapString(strippedUri.slice(workspacePath.size() + 1));
    } else {
      KJ_LOG(WARNING, "URI is not in workspace path", uri.cStr());
      return kj::READY_NOW;
    }

    KJ_LOG(
        INFO,
        "Definition request params:",
        strippedUri.cStr(),
        line,
        character);

    KJ_IF_MAYBE (rangeMap, fileSourceInfoMap.find(strippedUri)) {
      for (const auto &[range, id] : *rangeMap) {
        if (range.start.line <= line && line <= range.end.line &&
            range.start.character <= character &&
            character <= range.end.character) {

          KJ_LOG(INFO, "Found range for ", id);

          KJ_IF_MAYBE (location, nodeLocationMap.find(id)) {
            KJ_LOG(INFO, "Found location");

            auto locationObj = resultField.getValue().initObject(2);

            // Uri
            auto uriField = locationObj[0];
            uriField.setName("uri");
            kj::String fullUri =
                kj::str("file://", workspacePath, "/", (*location)->uri);
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
    }

    resultField.getValue().setNull();
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing definition request", e.getDescription());
    resultField.getValue().setNull();
  }

  return kj::READY_NOW;
}

kj::Promise<void> CapnpLanguageServer::onDidChangeWatchedFiles(
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
CapnpLanguageServer::onDidSave(const capnp::JsonValue::Reader &params) {
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

kj::Promise<void> CapnpLanguageServer::onInitialize(
    const capnp::JsonValue::Reader &params,
    capnp::MallocMessageBuilder &initializeResponseBuilder) {
  KJ_LOG(INFO, "Handling initialize request");

  try {
    auto paramsObj = params.getObject();
    for (auto field : paramsObj) {
      KJ_LOG(INFO, "Processing field", field.getName());
      if (field.getName() == "workspaceFolders") {
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
      } else if (field.getName() == "initializationOptions") {
        auto initOptions = field.getValue().getObject();
        for (auto optField : initOptions) {
          if (optField.getName() == "capnp") {
            auto capnpConfig = optField.getValue().getObject();
            for (auto configField : capnpConfig) {
              if (configField.getName() == "compilerPath") {
                compilerPath =
                    kj::heapString(configField.getValue().getString());
                KJ_LOG(INFO, "Compiler path set to", compilerPath);
              } else if (configField.getName() == "importPaths") {
                auto paths = configField.getValue().getArray();
                for (auto path : paths) {
                  importPaths.add(kj::heapString(path.getString()));
                }
                KJ_LOG(INFO, "Import paths configured");
              }
            }
          }
        }
      }
    }
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error processing initialize params", e.getDescription());
  }

  KJ_LOG(INFO, "Creating response message");
  auto root = initializeResponseBuilder.initRoot<capnp::JsonValue>();
  auto resultObj = root.initObject(1);
  auto resultField = resultObj[0];
  resultField.setName("result");

  auto resultValue = resultField.getValue().initObject(1);
  auto capsField = resultValue[0];
  capsField.setName("capabilities");

  auto capabilities = capsField.getValue().initObject(4);

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

  // Set completion provider capability
  auto compField = capabilities[2];
  compField.setName("completionProvider");
  compField.getValue().setBoolean(true);

  // Set workspace/didChangeWatchedFiles capability
  auto watchedFilesField = capabilities[3];
  watchedFilesField.setName("workspace/didChangeWatchedFiles");
  watchedFilesField.getValue().setBoolean(true);

  return kj::READY_NOW;
}

kj::Promise<void> CapnpLanguageServer::onDidOpenTextDocument(
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
    KJ_LOG(INFO, "URI", uri.cStr());

    return compileCapnpFile(uri);
  } catch (kj::Exception &e) {
    KJ_LOG(
        ERROR,
        "Error processing didOpenTextDocument notification",
        e.getDescription());
  }
  return kj::READY_NOW;
}
} // namespace capnp_ls