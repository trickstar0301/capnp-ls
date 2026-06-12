// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "log_level.h"
#include "symbol_index.h"
#include <kj/string.h>
#include <kj/vector.h>

namespace capnp_ls {

struct WorkspaceState {
  kj::String workspacePath;
  kj::Vector<kj::String> importPaths;
  bool importPathsConfiguredByInitialization = false;
  LogLevel defaultLogLevel = LogLevel::WARNING;
  LogLevel currentLogLevel = defaultLogLevel;

  bool reloadProjectConfig(SymbolIndex &index);
};

bool isProjectConfigPath(kj::StringPtr path);

} // namespace capnp_ls
