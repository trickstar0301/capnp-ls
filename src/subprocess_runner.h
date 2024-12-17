// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <capnp/message.h>
#include <kj/async-io.h>
#include <kj/function.h>

namespace capnp_ls {

class SubprocessRunner {
public:
  explicit SubprocessRunner(kj::AsyncIoContext &ioContext);
  KJ_DISALLOW_COPY(SubprocessRunner);

  struct RunParams {
    kj::StringPtr command;
    kj::StringPtr workingDir;
  };

  enum class Status { SUCCESS, WORKDIR_ERROR, EXECUTION_ERROR };

  struct RunResult {
    Status status;
    int exitCode;
    kj::Maybe<kj::Own<capnp::MessageReader>> maybeReader;
  };

  kj::Promise<RunResult> run(RunParams params);

private:
  bool setWorkingDirectory(const kj::StringPtr &workingDir);
  kj::AsyncIoContext &ioContext;
};
} // namespace capnp_ls