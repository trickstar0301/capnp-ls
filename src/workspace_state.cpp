// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "workspace_state.h"
#include "kj_compat.h"
#include "project_config.h"
#include <kj/debug.h>

namespace capnp_ls {

bool isProjectConfigPath(kj::StringPtr path) {
  return path.endsWith(kj::str("/", PROJECT_CONFIG_FILE)) ||
      path == PROJECT_CONFIG_FILE;
}

bool WorkspaceState::reloadProjectConfig(SymbolIndex &index) {
  if (workspacePath.size() == 0) {
    KJ_LOG(INFO, "Skipping project config reload because workspace path is not set");
    return false;
  }

  ProjectConfig loadedConfig;
  if (loadProjectConfig(workspacePath, loadedConfig)) {
    CAPNP_LS_IF_SOME (logLevel, loadedConfig.logLevel) {
      currentLogLevel = *logLevel;
    } else {
      currentLogLevel = defaultLogLevel;
    }
    applyLogLevel(currentLogLevel);

    if (!importPathsConfiguredByInitialization) {
      importPaths.clear();
      for (auto &path : loadedConfig.importPaths) {
        importPaths.add(kj::mv(path));
      }
    } else {
      KJ_LOG(INFO, "Skipping project config import paths because initialization options configured import paths");
    }

    index.clear();
    KJ_LOG(INFO, "Project config reloaded");
    return true;
  }

  if (!projectConfigExists(workspacePath)) {
    currentLogLevel = defaultLogLevel;
    applyLogLevel(currentLogLevel);
    if (!importPathsConfiguredByInitialization) {
      importPaths.clear();
    }
    index.clear();
    KJ_LOG(INFO, "Project config removed; defaults restored");
    return true;
  }

  KJ_LOG(ERROR, "Keeping previous project config because reload failed");
  return false;
}

} // namespace capnp_ls
