// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_message_handler.h"
#include "stdout_writer.h"
#include <capnp/common.h>
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <filesystem>
#include <fstream>
#include <kj/async-io.h>
#include <kj/debug.h>
#include <string>
#include <unistd.h>

namespace {

class CapturingOutputStream final : public kj::AsyncOutputStream {
public:
#if CAPNP_VERSION_MAJOR >= 2
  kj::Promise<void> write(kj::ArrayPtr<const kj::byte> buffer) override {
    output.append(reinterpret_cast<const char *>(buffer.begin()), buffer.size());
    return kj::READY_NOW;
  }
#else
  kj::Promise<void> write(const void *buffer, size_t size) override {
    output.append(reinterpret_cast<const char *>(buffer), size);
    return kj::READY_NOW;
  }
#endif

  kj::Promise<void>
  write(kj::ArrayPtr<const kj::ArrayPtr<const kj::byte>> pieces) override {
    for (auto piece : pieces) {
      output.append(reinterpret_cast<const char *>(piece.begin()), piece.size());
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  std::string output;
};

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

std::filesystem::path makeTempWorkspace() {
  auto path = std::filesystem::temp_directory_path() /
      kj::str("capnp-ls-completion-test-", getpid()).cStr();
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void writeSchema(const std::filesystem::path &path, kj::StringPtr content) {
  std::ofstream output(path);
  output.write(content.begin(), content.size());
}

void decodeJson(kj::StringPtr json, capnp::MallocMessageBuilder &builder) {
  capnp::JsonCodec codec;
  auto root = builder.initRoot<capnp::JsonValue>();
  codec.decodeRaw(kj::arrayPtr(json.begin(), json.size()), root);
}

kj::String lspMessage(kj::StringPtr json) {
  return kj::str("Content-Length: ", json.size(), "\r\n\r\n", json);
}

kj::StringPtr responseJson(kj::StringPtr message) {
  kj::StringPtr delimiter = "\r\n\r\n";
  for (auto i = 0; i + delimiter.size() <= message.size(); ++i) {
    if (message.slice(i, i + delimiter.size()) == delimiter) {
      return message.slice(i + delimiter.size());
    }
  }
  KJ_FAIL_REQUIRE("response should include an LSP header");
}

capnp::JsonValue::Reader getObjectField(
    const capnp::JsonValue::Reader &value,
    kj::StringPtr fieldName) {
  for (auto field : value.getObject()) {
    if (field.getName() == fieldName) {
      return field.getValue();
    }
  }

  KJ_FAIL_REQUIRE("missing field", fieldName);
}

} // namespace

int main() {
  auto io = kj::setupAsyncIo();
  auto paf = kj::newPromiseAndFulfiller<void>();
  capnp_ls::ServerContext context(kj::mv(paf.fulfiller));

  auto workspace = makeTempWorkspace();
  KJ_DEFER(std::filesystem::remove_all(workspace));
  auto schemaPath = workspace / "person.capnp";
  auto schemaPathString = schemaPath.string();
  // The on-disk file stays one edit behind the editor buffer so the test can
  // tell which one completion reads.
  writeSchema(
      schemaPath,
      R"(@0xdba53d6c0e9fe303;
struct Person {
  name @0 :Text;
  age @1 :UInt32;
}
)");

  auto stream = kj::heap<CapturingOutputStream>();
  auto &captured = *stream;
  capnp_ls::StdoutWriter writer(kj::mv(stream));
  capnp_ls::LspMessageHandler handler(context, writer);

  auto workspaceString = workspace.string();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"workspaceFolders":[{"uri":"file://)",
          workspaceString.c_str(),
          R"("}]}})")))
      .wait(io.waitScope);

  {
    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto root = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(root, "result");
    auto capabilities = getObjectField(result, "capabilities");
    auto completionProvider = getObjectField(capabilities, "completionProvider");
    auto triggerCharacters =
        getObjectField(completionProvider, "triggerCharacters");
    require(
        triggerCharacters.isArray() && triggerCharacters.getArray().size() == 1 &&
            triggerCharacters.getArray()[0].getString() == "@",
        "initialize should advertise '@' as the completion trigger character");
  }

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"(","languageId":"capnp","version":1,"text":"@0xdba53d6c0e9fe303;\nstruct Person {\n  name @0 :Text;\n  age @1 :UInt32;\n  email @\n}\n"}}})")))
      .wait(io.waitScope);

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"("},"position":{"line":4,"character":9},"context":{"triggerKind":2,"triggerCharacter":"@"}}})")))
      .wait(io.waitScope);

  {
    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto root = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(root, "result");
    require(
        result.isArray() && result.getArray().size() == 1,
        "completion should return exactly one item");
    auto item = result.getArray()[0];
    require(
        getObjectField(item, "label").getString() == "2",
        "completion should suggest the next ordinal from the open buffer");
    require(
        getObjectField(item, "kind").getNumber() == 12,
        "completion item should use the Value kind");
    auto textEdit = getObjectField(item, "textEdit");
    require(
        getObjectField(textEdit, "newText").getString() == "2",
        "completion text edit should insert the next ordinal");
    auto range = getObjectField(textEdit, "range");
    auto start = getObjectField(range, "start");
    auto end = getObjectField(range, "end");
    require(
        getObjectField(start, "line").getNumber() == 4 &&
            getObjectField(start, "character").getNumber() == 9 &&
            getObjectField(end, "line").getNumber() == 4 &&
            getObjectField(end, "character").getNumber() == 9,
        "completion text edit range should be empty at the cursor");
  }

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"(","version":2},"contentChanges":[{"text":"@0xdba53d6c0e9fe303;\nstruct Person {\n  name @0 :Text;\n  age @1 :UInt32;\n  phone @2 :Text;\n  email @\n}\n"}]}})")))
      .wait(io.waitScope);

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"("},"position":{"line":5,"character":9}}})")))
      .wait(io.waitScope);

  {
    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto root = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(root, "result");
    require(
        result.isArray() && result.getArray().size() == 1 &&
            getObjectField(result.getArray()[0], "label").getString() == "3",
        "completion should reflect didChange edits, not the file on disk");
  }

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"(","version":3},"contentChanges":[{"text":"@0xdba53d6c0e9fe303;\nstruct Person {\n  name @0 :Text;\n  un\n}\n"}]}})")))
      .wait(io.waitScope);

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"("},"position":{"line":3,"character":4}}})")))
      .wait(io.waitScope);

  {
    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto root = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(root, "result");
    require(
        result.isArray() && result.getArray().size() == 7,
        "keyword completion in a struct body should offer seven keywords");
    bool foundUnion = false;
    for (auto item : result.getArray()) {
      if (getObjectField(item, "label").getString() != "union") {
        continue;
      }
      foundUnion = true;
      require(
          getObjectField(item, "kind").getNumber() == 14,
          "keyword completion items should use the Keyword kind");
      auto textEdit = getObjectField(item, "textEdit");
      require(
          getObjectField(textEdit, "newText").getString() == "union",
          "keyword text edit should insert the keyword");
      auto range = getObjectField(textEdit, "range");
      auto start = getObjectField(range, "start");
      auto end = getObjectField(range, "end");
      require(
          getObjectField(start, "line").getNumber() == 3 &&
              getObjectField(start, "character").getNumber() == 2 &&
              getObjectField(end, "line").getNumber() == 3 &&
              getObjectField(end, "character").getNumber() == 4,
          "keyword text edit range should cover the typed prefix");
    }
    require(foundUnion, "struct body keyword completion should include union");
  }

  captured.output.clear();
  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"("}}})")))
      .wait(io.waitScope);

  handler
      .handleMessage(lspMessage(kj::str(
          R"({"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://)",
          schemaPathString.c_str(),
          R"("},"position":{"line":4,"character":9}}})")))
      .wait(io.waitScope);

  {
    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto root = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(root, "result");
    require(
        result.isArray() && result.getArray().size() == 0,
        "completion after didClose should return no items");
  }

  return 0;
}
