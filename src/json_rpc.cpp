// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "json_rpc.h"
#include "kj_compat.h"
#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <kj/debug.h>
#include <kj/string.h>

namespace capnp_ls {

kj::String frameLspMessage(kj::StringPtr json) {
  return kj::str(LSP_CONTENT_LENGTH_HEADER, json.size(), LSP_HEADER_DELIMITER, json);
}

kj::Maybe<kj::String>
buildResponseString(const double id, const capnp::JsonValue::Reader &result) {
  try {
    capnp::MallocMessageBuilder messageBuilder;
    auto root = messageBuilder.initRoot<capnp::JsonValue>();
    auto obj = root.initObject(3);

    obj[0].setName(LSP_JSONRPC);
    obj[0].getValue().setString(LSP_JSON_RPC_VERSION);

    obj[1].setName(LSP_ID);
    obj[1].getValue().setNumber(id);

    obj[2].setName(LSP_RESULT);
    if (!result.isObject() || result.getObject().size() == 0) {
      obj[2].getValue().setNull();
    } else {
      auto resultValue = result.getObject()[0].getValue();
      if (resultValue.isObject()) {
        obj[2].getValue().setObject(resultValue.getObject());
      } else if (resultValue.isArray()) {
        obj[2].getValue().setArray(resultValue.getArray());
      } else if (resultValue.isString()) {
        obj[2].getValue().setString(resultValue.getString());
      } else if (resultValue.isNumber()) {
        obj[2].getValue().setNumber(resultValue.getNumber());
      } else if (resultValue.isBoolean()) {
        obj[2].getValue().setBoolean(resultValue.getBoolean());
      } else {
        obj[2].getValue().setNull();
      }
    }

    capnp::JsonCodec codec;
    kj::String responseStr =
        codec.encodeRaw(messageBuilder.getRoot<capnp::JsonValue>());

    return frameLspMessage(responseStr);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error building response string", e.getDescription());
    return CAPNP_LS_NONE;
  }
}

kj::Maybe<kj::String>
buildErrorResponseString(const double id, int code, kj::StringPtr message) {
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

    return frameLspMessage(responseStr);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error building error response string", e.getDescription());
    return CAPNP_LS_NONE;
  }
}

} // namespace capnp_ls
