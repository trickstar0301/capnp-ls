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
  capnp::JsonCodec codec;
  capnp::MallocMessageBuilder messageBuilder;
  auto root = messageBuilder.initRoot<capnp::JsonValue>();
  kj::ArrayPtr<const char> jsonContent(content.begin(), content.size());
  codec.decodeRaw(jsonContent, root);

  auto config = root.getObject();
  for (auto field : config) {
    if (field.getName().asString() == kj::StringPtr("importPaths")) {
      auto paths = field.getValue().getArray();
      for (auto path : paths) {
        importPaths.add(kj::heapString(kj::StringPtr(path.getString())));
      }
    }
  }
}

bool loadProjectConfigImportPaths(
    kj::StringPtr workspacePath,
    kj::Vector<kj::String> &importPaths) {
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
    parseProjectConfigImportPaths(kj::StringPtr(content.data(), content.size()), importPaths);
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Failed to parse project config", configPath, e.getDescription());
    return false;
  }

  KJ_LOG(INFO, "Loaded project config", configPath);
  return true;
}

bool projectConfigExists(kj::StringPtr workspacePath) {
  auto configPath = projectConfigPath(workspacePath);
  std::ifstream input(configPath.cStr());
  return input.good();
}

} // namespace capnp_ls
