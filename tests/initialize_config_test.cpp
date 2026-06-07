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

void decodeJson(kj::StringPtr json, capnp::MallocMessageBuilder &builder) {
  capnp::JsonCodec codec;
  auto root = builder.initRoot<capnp::JsonValue>();
  codec.decodeRaw(kj::arrayPtr(json.begin(), json.size()), root);
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
        handler.testImportPathCount() == 2,
        "rootUri initialize should load .capnp-ls.json import paths");
  }

  {
    auto workspace = makeTempWorkspace();
    KJ_DEFER(std::filesystem::remove_all(workspace));
    writeConfig(workspace, R"({"importPaths":["one"]})");

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

    writeConfig(workspace, R"({"importPaths":["two","three"]})");
    require(handler.testReloadProjectConfig(), "valid config change should reload");
    require(
        handler.testImportPathCount() == 2 && handler.testImportPath(0) == "two",
        "changed project config should replace import paths");

    writeConfig(workspace, R"({"importPaths":[)");
    require(!handler.testReloadProjectConfig(), "invalid config should not reload");
    require(
        handler.testImportPathCount() == 2 && handler.testImportPath(0) == "two",
        "invalid project config should keep previous import paths");

    std::filesystem::remove(workspace / ".capnp-ls.json");
    require(handler.testReloadProjectConfig(), "deleted config should reload");
    require(
        handler.testImportPathCount() == 0,
        "deleted project config should clear import paths");
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

  return 0;
}
