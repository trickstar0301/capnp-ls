// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "project_config.h"

#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <fstream>
#include <iterator>
#include <kj/debug.h>

namespace capnp_ls {

namespace {

kj::String projectConfigPath(kj::StringPtr workspacePath) {
  return kj::str(workspacePath, "/", PROJECT_CONFIG_FILE);
}

} // namespace

void parseProjectConfigImportPaths(
    kj::StringPtr content,
    kj::Vector<kj::String> &importPaths) {
  ProjectConfig config;
  parseProjectConfig(content, config);
  for (auto &path : config.importPaths) {
    importPaths.add(kj::mv(path));
  }
}

void parseProjectConfig(kj::StringPtr content, ProjectConfig &config) {
  capnp::JsonCodec codec;
  capnp::MallocMessageBuilder messageBuilder;
  auto root = messageBuilder.initRoot<capnp::JsonValue>();
  kj::ArrayPtr<const char> jsonContent(content.begin(), content.size());
  codec.decodeRaw(jsonContent, root);

  auto configObject = root.getObject();
  for (auto field : configObject) {
    if (field.getName().asString() == kj::StringPtr("importPaths")) {
      auto paths = field.getValue().getArray();
      for (auto path : paths) {
        config.importPaths.add(kj::heapString(kj::StringPtr(path.getString())));
      }
    } else if (field.getName().asString() == kj::StringPtr("logLevel")) {
      KJ_REQUIRE(field.getValue().isString(), "logLevel must be a string");
      CAPNP_LS_IF_SOME (level, parseLogLevel(field.getValue().getString())) {
        config.logLevel = *level;
      } else {
        KJ_FAIL_REQUIRE(
            "logLevel must be one of: error, warning, info",
            field.getValue().getString());
      }
    }
  }
}

bool loadProjectConfig(kj::StringPtr workspacePath, ProjectConfig &config) {
  auto configPath = projectConfigPath(workspacePath);
  std::ifstream input(configPath.cStr());
  if (!input.good()) {
    KJ_LOG(INFO, "Project config not found", configPath);
    return false;
  }

  std::string content(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());

  try {
    parseProjectConfig(kj::StringPtr(content.data(), content.size()), config);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Failed to parse project config", configPath, e.getDescription());
    return false;
  }

  KJ_LOG(INFO, "Loaded project config", configPath);
  return true;
}

bool loadProjectConfigImportPaths(
    kj::StringPtr workspacePath,
    kj::Vector<kj::String> &importPaths) {
  ProjectConfig config;
  if (!loadProjectConfig(workspacePath, config)) {
    return false;
  }

  for (auto &path : config.importPaths) {
    importPaths.add(kj::mv(path));
  }
  return true;
}

bool projectConfigExists(kj::StringPtr workspacePath) {
  auto configPath = projectConfigPath(workspacePath);
  std::ifstream input(configPath.cStr());
  return input.good();
}

} // namespace capnp_ls
