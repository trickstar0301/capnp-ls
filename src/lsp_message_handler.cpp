// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_message_handler.h"
#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <iostream>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string.h>
#include <unistd.h>

namespace capnp_ls {

kj::Promise<kj::Maybe<kj::String>>
LspMessageHandler::handleMessage(kj::Maybe<kj::StringPtr> maybeMessage) {
  try {
    KJ_IF_MAYBE (message, maybeMessage) {
      const char *headerEnd = strstr(message->begin(), LSP_HEADER_DELIMITER);
      if (!headerEnd) {
        KJ_LOG(ERROR, "Invalid message format: no header delimiter found");
        server->onShutdown();
        return kj::Promise<kj::Maybe<kj::String>>(nullptr);
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

      KJ_IF_MAYBE (methodEnum, tryParseLspMethod(method)) {
        switch (*methodEnum) {
        case LspMethod::INITIALIZE:
          promise = server->onInitialize(params, *responseMessageBuilder);
          break;
        case LspMethod::SHUTDOWN:
          promise = server->onShutdown();
          break;
        case LspMethod::DEFINITION:
          promise = server->onDefinition(params, *responseMessageBuilder);
          break;
        case LspMethod::DID_OPEN:
          promise = server->onDidOpenTextDocument(params);
          break;
        case LspMethod::DID_SAVE:
          promise = server->onDidSave(params);
          break;
        case LspMethod::INITIALIZED:
        case LspMethod::SET_TRACE:
        case LspMethod::CANCEL_REQUEST:
        case LspMethod::DID_CHANGE_WATCHED_FILES:
        case LspMethod::DID_CHANGE:
          KJ_LOG(INFO, "Ignoring method", method.cStr());
          break;
        }
      } else {
        KJ_LOG(ERROR, "Unknown method", method.cStr());
      }

      KJ_IF_MAYBE (requestId, maybeRequestId) {
        return promise.then(
            [this,
             id = *requestId,
             builder = kj::mv(responseMessageBuilder)]() mutable {
              auto response = builder->getRoot<capnp::JsonValue>().asReader();
              return buildResponseString(id, response);
            });
      } else {
        KJ_LOG(INFO, "No request id found");
        return promise.then([]() { return kj::Maybe<kj::String>(nullptr); });
      }

    } else {
      KJ_LOG(INFO, "EOF detected on stdin, initiating shutdown...");
      server->onShutdown();
    }
  } catch (const std::exception &e) {
    KJ_LOG(ERROR, "Error processing message", e.what());
  }
  return kj::Promise<kj::Maybe<kj::String>>(nullptr);
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
    KJ_LOG(INFO, "Encoded response", responseStr.cStr());

    return kj::str(
        LSP_CONTENT_LENGTH_HEADER,
        responseStr.size(),
        LSP_HEADER_DELIMITER,
        responseStr);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error building response string", e.getDescription());
    return nullptr;
  }
}
} // namespace capnp_ls