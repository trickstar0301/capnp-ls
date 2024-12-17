// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <cerrno>
#include <cstdio>
#include <kj/debug.h>
#include <kj/exception.h>

namespace capnp_ls {
class FileLogger : public kj::ExceptionCallback {
public:
  FileLogger(const char *filename) : ExceptionCallback() {
    kj::_::Debug::setLogLevel(kj::LogSeverity::WARNING);
    file = fopen(filename, "w");
    if (file == nullptr) {
      KJ_FAIL_SYSCALL("fopen", errno);
    }
  }

  ~FileLogger() {
    if (file != nullptr) {
      fclose(file);
    }
  }

  void logMessage(kj::LogSeverity severity, const char *file, int line, int,
                  kj::String &&text) override {
    fprintf(this->file, "%s:%d: %s: %s\n", file, line, kj::str(severity).cStr(),
            text.cStr());
    fflush(this->file);
  }

private:
  FILE *file;
};
} // namespace capnp_ls