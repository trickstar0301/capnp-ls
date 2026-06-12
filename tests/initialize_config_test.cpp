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

class NullOutputStream final : public kj::AsyncOutputStream {
public:
#if CAPNP_VERSION_MAJOR >= 2
  kj::Promise<void> write(kj::ArrayPtr<const kj::byte> buffer) override {
    return kj::READY_NOW;
  }
#else
  kj::Promise<void> write(const void *buffer, size_t size) override {
    return kj::READY_NOW;
  }
#endif

  kj::Promise<void>
  write(kj::ArrayPtr<const kj::ArrayPtr<const kj::byte>> pieces) override {
    return kj::READY_NOW;
  }

  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

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
      kj::str("capnp-ls-initialize-config-test-", getpid()).cStr();
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void writeConfig(const std::filesystem::path &workspace, kj::StringPtr content) {
  std::ofstream output(workspace / ".capnp-ls.json");
  output.write(content.begin(), content.size());
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

bool hasObjectField(
    const capnp::JsonValue::Reader &value,
    kj::StringPtr fieldName) {
  if (!value.isObject()) {
    return false;
  }

  for (auto field : value.getObject()) {
    if (field.getName() == fieldName) {
      return true;
    }
  }

  return false;
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

bool arrayHasObjectWithStringField(
    const capnp::JsonValue::Reader &value,
    kj::StringPtr fieldName,
    kj::StringPtr expected) {
  if (!value.isArray()) {
    return false;
  }

  for (auto item : value.getArray()) {
    if (!item.isObject()) {
      continue;
    }
    for (auto field : item.getObject()) {
      if (field.getName() == fieldName && field.getValue().isString() &&
          field.getValue().getString() == expected) {
        return true;
      }
    }
  }

  return false;
}

} // namespace

int main() {
  auto io = kj::setupAsyncIo();
  auto paf = kj::newPromiseAndFulfiller<void>();
  capnp_ls::ServerContext context(io, kj::mv(paf.fulfiller));

  {
    capnp_ls::StdoutWriter writer(kj::heap<NullOutputStream>());
    capnp_ls::LspMessageHandler handler(context, writer);
    capnp::MallocMessageBuilder params;
    decodeJson(kj::str(
        R"({"workspaceFolders":null,"rootUri":"file://)",
        CAPNP_LS_TEST_FIXTURE_DIR,
        R"("})"),
        params);
    capnp::MallocMessageBuilder response;
    handler
        .testHandleInitialize(params.getRoot<capnp::JsonValue>().asReader(), response)
        .wait(io.waitScope);
    require(
        handler.testWorkspacePath() == CAPNP_LS_TEST_FIXTURE_DIR,
        "rootUri should be used when workspaceFolders is null");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::WARNING,
        "server log level should default to warning");
    require(
        handler.testImportPathCount() == 2,
        "rootUri initialize should load .capnp-ls.json import paths");

    auto responseRoot = response.getRoot<capnp::JsonValue>().asReader();
    auto result = getObjectField(responseRoot, "result");
    auto capabilities = getObjectField(result, "capabilities");
    require(
        !hasObjectField(capabilities, "completionProvider"),
        "initialize should not advertise completion without a completion handler");
    require(
        hasObjectField(capabilities, "hoverProvider"),
        "initialize should advertise hover support");
    require(
        hasObjectField(capabilities, "referencesProvider"),
        "initialize should advertise references support");
    require(
        hasObjectField(capabilities, "documentSymbolProvider"),
        "initialize should advertise document symbol support");
  }

  {
    auto workspace = makeTempWorkspace();
    KJ_DEFER(std::filesystem::remove_all(workspace));
    writeConfig(workspace, R"({"importPaths":["one"],"logLevel":"info"})");

    capnp_ls::StdoutWriter writer(kj::heap<NullOutputStream>());
    capnp_ls::LspMessageHandler handler(context, writer);
    capnp::MallocMessageBuilder params;
    auto workspaceString = workspace.string();
    decodeJson(kj::str(
        R"({"workspaceFolders":[{"uri":"file://)",
        workspaceString.c_str(),
        R"("}]})"),
        params);
    capnp::MallocMessageBuilder response;
    handler
        .testHandleInitialize(params.getRoot<capnp::JsonValue>().asReader(), response)
        .wait(io.waitScope);
    require(
        handler.testImportPathCount() == 1 && handler.testImportPath(0) == "one",
        "initial import path should load from project config");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::INFO,
        "initial log level should load from project config");

    writeConfig(workspace, R"({"importPaths":["two","three"],"logLevel":"warning"})");
    auto configPathString = (workspace / ".capnp-ls.json").string();
    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":"file://)",
            configPathString.c_str(),
            R"(","type":2}]}})")))
        .wait(io.waitScope);
    require(
        handler.testImportPathCount() == 2 && handler.testImportPath(0) == "two",
        "watched file change should reload project config import paths");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::WARNING,
        "watched file change should reload project config log level");

    writeConfig(workspace, R"({"importPaths":[)");
    require(!handler.testReloadProjectConfig(), "invalid config should not reload");
    require(
        handler.testImportPathCount() == 2 && handler.testImportPath(0) == "two",
        "invalid project config should keep previous import paths");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::WARNING,
        "invalid project config should keep previous log level");

    writeConfig(workspace, R"({"importPaths":["four"],"logLevel":"debug"})");
    require(
        !handler.testReloadProjectConfig(),
        "unknown project config log level should not reload");
    require(
        handler.testImportPathCount() == 2 && handler.testImportPath(0) == "two",
        "unknown project config log level should keep previous import paths");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::WARNING,
        "unknown project config log level should keep previous log level");

    std::filesystem::remove(workspace / ".capnp-ls.json");
    require(handler.testReloadProjectConfig(), "deleted config should reload");
    require(
        handler.testImportPathCount() == 0,
        "deleted project config should clear import paths");
    require(
        handler.testLogLevel() == capnp_ls::LogLevel::WARNING,
        "deleted project config should restore default log level");
  }

  {
    capnp_ls::StdoutWriter writer(kj::heap<NullOutputStream>());
    capnp_ls::LspMessageHandler handler(context, writer);
    capnp::MallocMessageBuilder params;
    decodeJson(kj::str(
        R"({"workspaceFolders":[{"uri":"file://)",
        CAPNP_LS_TEST_FIXTURE_DIR,
        R"("}],"initializationOptions":{"capnp":{"importPaths":["override"]}}})"),
        params);
    capnp::MallocMessageBuilder response;
    handler
        .testHandleInitialize(params.getRoot<capnp::JsonValue>().asReader(), response)
        .wait(io.waitScope);
    require(
        handler.testImportPathCount() == 1,
        "initialization options should override .capnp-ls.json");
    require(
        handler.testImportPath(0) == "override",
        "initialization option import path should be preserved");
  }

  {
    auto workspace = makeTempWorkspace();
    KJ_DEFER(std::filesystem::remove_all(workspace));
    auto schemaPath = workspace / "syntax.capnp";
    auto schemaPathString = schemaPath.string();
    writeSchema(
        schemaPath,
        R"(@0xdba53d6c0e9fe303;
const defaultName :Text = "Ada";
struct Person {
  name @0 :Text = defaultName;
}
)");

    auto stream = kj::heap<CapturingOutputStream>();
    auto &captured = *stream;
    capnp_ls::StdoutWriter writer(kj::mv(stream));
    capnp_ls::LspMessageHandler handler(context, writer);

    capnp::MallocMessageBuilder params;
    auto workspaceString = workspace.string();
    decodeJson(kj::str(
        R"({"workspaceFolders":[{"uri":"file://)",
        workspaceString.c_str(),
        R"("}]})"),
        params);
    capnp::MallocMessageBuilder response;
    handler
        .testHandleInitialize(params.getRoot<capnp::JsonValue>().asReader(), response)
        .wait(io.waitScope);

    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://)",
            schemaPathString.c_str(),
            R"("}}})")))
        .wait(io.waitScope);
    require(
        captured.output.find("Constant names must be qualified") !=
            std::string::npos,
        "invalid schema should publish a compiler diagnostic");

    captured.output.clear();
    writeSchema(
        schemaPath,
        R"(@0xdba53d6c0e9fe303;
const defaultName :Text = "Ada";
struct Person {
  name @0 :Text = .defaultName;
}
)");
    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"file://)",
            schemaPathString.c_str(),
            R"("}}})")))
        .wait(io.waitScope);
    require(
        captured.output.find(R"("diagnostics":[])") != std::string::npos,
        "fixed schema should clear previous diagnostics");
    require(
        captured.output.find("Constant names must be qualified") ==
            std::string::npos,
        "fixed schema should not republish the previous diagnostic");

    captured.output.clear();
    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","id":7,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file://)",
            schemaPathString.c_str(),
            R"("}}})")))
        .wait(io.waitScope);
    capnp::MallocMessageBuilder documentSymbolResponse;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        documentSymbolResponse);
    auto documentSymbolRoot =
        documentSymbolResponse.getRoot<capnp::JsonValue>().asReader();
    auto documentSymbolResult = getObjectField(documentSymbolRoot, "result");
    require(
        arrayHasObjectWithStringField(documentSymbolResult, "name", "Person"),
        "documentSymbol should return compiled document symbols as an array result");

    captured.output.clear();
    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","id":8,"method":"textDocument/hover","params":{"textDocument":{"uri":"file://)",
            schemaPathString.c_str(),
            R"("},"position":{"line":1,"character":8}}})")))
        .wait(io.waitScope);
    capnp::MallocMessageBuilder hoverResponse;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        hoverResponse);
    auto hoverRoot = hoverResponse.getRoot<capnp::JsonValue>().asReader();
    auto hoverResult = getObjectField(hoverRoot, "result");
    auto hoverContents = getObjectField(hoverResult, "contents");
    auto hoverValue = getObjectField(hoverContents, "value");
    require(
        std::string(hoverValue.getString().cStr()).find("const defaultName") !=
            std::string::npos,
        "hover should return symbol metadata");

    captured.output.clear();
    handler
        .handleMessage(lspMessage(kj::str(
            R"({"jsonrpc":"2.0","id":9,"method":"textDocument/references","params":{"textDocument":{"uri":"file://)",
            schemaPathString.c_str(),
            R"("},"position":{"line":1,"character":8},"context":{"includeDeclaration":true}}})")))
        .wait(io.waitScope);
    capnp::MallocMessageBuilder referencesResponse;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        referencesResponse);
    auto referencesRoot = referencesResponse.getRoot<capnp::JsonValue>().asReader();
    auto referencesResult = getObjectField(referencesRoot, "result");
    require(
        referencesResult.isArray() && referencesResult.getArray().size() > 0,
        "references should return locations as an array result");
  }

  {
    auto stream = kj::heap<CapturingOutputStream>();
    auto &captured = *stream;
    capnp_ls::StdoutWriter writer(kj::mv(stream));
    capnp_ls::LspMessageHandler handler(context, writer);
    handler
        .handleMessage(kj::str(
            "Content-Length: 67\r\n\r\n",
            R"({"jsonrpc":"2.0","id":42,"method":"textDocument/completion"})"))
        .wait(io.waitScope);

    capnp::MallocMessageBuilder response;
    decodeJson(
        responseJson(
            kj::StringPtr(captured.output.data(), captured.output.size())),
        response);
    auto responseRoot = response.getRoot<capnp::JsonValue>().asReader();
    auto error = getObjectField(responseRoot, "error");
    auto code = getObjectField(error, "code");
    auto message = getObjectField(error, "message");
    require(
        code.getNumber() == -32601,
        "unknown request should return Method not found");
    require(
        message.getString() == "Method not found",
        "unknown request should include a Method not found message");
    require(
        !hasObjectField(responseRoot, "result"),
        "unknown request response should not include result");
  }

  return 0;
}
