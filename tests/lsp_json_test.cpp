// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_json.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <kj/debug.h>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

} // namespace

int main() {
  // Test parseTextDocumentPosition: LSP wire format (0-based) converts to
  // internal 1-based convention
  // NOTE: This is the single place the 1-based-internal / 0-based-wire
  // convention is enforced. LSP sends {line:5, character:3}, which represents
  // the 6th line and 4th character in internal (1-based) indexing.
  {
    capnp::MallocMessageBuilder builder;
    auto root = builder.initRoot<capnp::JsonValue>();
    auto paramsObj = root.initObject(2);

    // Set textDocument field with uri
    paramsObj[0].setName("textDocument");
    auto textDocObj = paramsObj[0].getValue().initObject(1);
    textDocObj[0].setName("uri");
    textDocObj[0].getValue().setString("file:///tmp/a.capnp");

    // Set position field with line and character (0-based from wire)
    paramsObj[1].setName("position");
    auto posObj = paramsObj[1].getValue().initObject(2);
    posObj[0].setName("line");
    posObj[0].getValue().setNumber(5);
    posObj[1].setName("character");
    posObj[1].getValue().setNumber(3);

    auto reader = builder.getRoot<capnp::JsonValue>();
    auto parsed = capnp_ls::parseTextDocumentPosition(reader);

    require(parsed.line == 6, "line should be 6 (1-based, wire was 5)");
    require(parsed.character == 4, "character should be 4 (1-based, wire was 3)");
    require(parsed.path == "/tmp/a.capnp", "path should be /tmp/a.capnp");
  }

  // Test setPosition: internal 1-based Position{6,4} serializes to wire
  // 0-based {line:5, character:3}
  {
    capnp::MallocMessageBuilder builder;
    auto root = builder.initRoot<capnp::JsonValue>();
    capnp_ls::Position pos{6, 4};
    capnp_ls::setPosition(root, pos);

    auto reader = builder.getRoot<capnp::JsonValue>();
    auto obj = reader.getObject();

    // Verify the JSON structure
    require(obj.size() == 2, "position object should have 2 fields");

    double lineValue = 0;
    double charValue = 0;

    for (auto field : obj) {
      if (field.getName() == "line") {
        lineValue = field.getValue().getNumber();
      } else if (field.getName() == "character") {
        charValue = field.getValue().getNumber();
      }
    }

    require(lineValue == 5, "wire line should be 5 (0-based, internal was 6)");
    require(charValue == 3, "wire character should be 3 (0-based, internal was 4)");
  }

  return 0;
}
