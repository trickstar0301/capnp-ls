// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <kj/debug.h>
#include <kj/string.h>

namespace capnp_ls {

enum class LogLevel { ERROR, WARNING, INFO };

inline kj::Maybe<LogLevel> parseLogLevel(kj::StringPtr level) {
  if (level == "error") {
    return LogLevel::ERROR;
  }
  if (level == "warning") {
    return LogLevel::WARNING;
  }
  if (level == "info") {
    return LogLevel::INFO;
  }

  return kj::none;
}

inline kj::LogSeverity toKjLogSeverity(LogLevel level) {
  switch (level) {
  case LogLevel::ERROR:
    return kj::LogSeverity::ERROR;
  case LogLevel::WARNING:
    return kj::LogSeverity::WARNING;
  case LogLevel::INFO:
    return kj::LogSeverity::INFO;
  }

  KJ_UNREACHABLE;
}

inline void applyLogLevel(LogLevel level) {
  kj::_::Debug::setLogLevel(toKjLogSeverity(level));
}

} // namespace capnp_ls
