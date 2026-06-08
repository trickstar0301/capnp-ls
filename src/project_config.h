// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "log_level.h"
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

constexpr const char *PROJECT_CONFIG_FILE = ".capnp-ls.json";

struct ProjectConfig {
  kj::Vector<kj::String> importPaths;
  kj::Maybe<LogLevel> logLevel;
};

void parseProjectConfig(kj::StringPtr content, ProjectConfig &config);

bool loadProjectConfig(kj::StringPtr workspacePath, ProjectConfig &config);

void parseProjectConfigImportPaths(
    kj::StringPtr content,
    kj::Vector<kj::String> &importPaths);

bool loadProjectConfigImportPaths(
    kj::StringPtr workspacePath,
    kj::Vector<kj::String> &importPaths);

bool projectConfigExists(kj::StringPtr workspacePath);

} // namespace capnp_ls
