// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_json.h"
#include "utils.h"
#include <kj/string.h>

namespace capnp_ls {

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

} // namespace capnp_ls
