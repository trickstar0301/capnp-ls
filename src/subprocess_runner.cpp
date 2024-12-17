// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "subprocess_runner.h"
#include "logger.h"
#include <capnp/message.h>
#include <capnp/serialize-async.h>
#include <cstdio>
#include <kj/common.h>
#include <kj/string.h>
#include <limits.h>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace capnp_ls {

SubprocessRunner::SubprocessRunner(kj::AsyncIoContext &ioContext)
    : ioContext(ioContext) {}

bool SubprocessRunner::setWorkingDirectory(const kj::StringPtr &workingDir) {
  if (workingDir == nullptr) {
    KJ_LOG(ERROR, "Working directory is not specified");
    return false;
  }

  if (chdir(workingDir.cStr()) != 0) {
    KJ_LOG(ERROR, "Failed to change directory", workingDir);
    return false;
  }

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == nullptr) {
    KJ_LOG(ERROR, "Failed to get current working directory");
    return false;
  }

  setenv("PWD", cwd, 1);
  KJ_LOG(INFO, "Working directory for capnp compile", workingDir);
  return true;
}

kj::Vector<kj::String> buildArgs(kj::StringPtr command) {
  // Build arguments for execv, separated by spaces
  kj::Vector<kj::String> argv;

  // Split command string by spaces
  size_t start = 0;
  while (start < command.size()) {
    // Skip spaces
    while (start < command.size() && command[start] == ' ')
      start++;
    if (start == command.size())
      break;

    // Find end of token
    size_t end = start;
    while (end < command.size() && command[end] != ' ')
      end++;

    // Add token
    argv.add(kj::str(command.slice(start, end)));
    start = end;
  }

  return argv;
}

kj::Promise<SubprocessRunner::RunResult>
SubprocessRunner::run(RunParams params) {
  if (!setWorkingDirectory(params.workingDir)) {
    KJ_LOG(ERROR, "Failed to set working directory", params.workingDir);
    return RunResult{.status = Status::WORKDIR_ERROR};
  }

  int pipeFds[2];
  KJ_SYSCALL(pipe(pipeFds));

  pid_t child;
  KJ_SYSCALL(child = fork());

  if (child == 0) {
    // Child process
    KJ_SYSCALL(close(pipeFds[0]));               // Close read end
    KJ_SYSCALL(dup2(pipeFds[1], STDOUT_FILENO)); // Connect stdout to pipe
    KJ_SYSCALL(close(pipeFds[1]));

    // Execute command
    auto argv = buildArgs(params.command);
    // Create a vector of char* for execv
    kj::Vector<const char *> argPtrs;
    for (auto &arg : argv) {
      argPtrs.add(arg.cStr());
    }
    // Add null terminator required by execv
    argPtrs.add(nullptr);
    KJ_LOG(INFO, "Executing command:", params.command);
    if (argv[0].startsWith("/")) {
      execv(argv[0].cStr(), const_cast<char *const *>(argPtrs.begin()));
    } else {
      execvp(argv[0].cStr(), const_cast<char *const *>(argPtrs.begin()));
    }
    // If exec fails
    int error = errno;
    if (error == ENOENT) {
      KJ_LOG(ERROR, "Command not found:", argv[0]);
    } else {
      KJ_FAIL_SYSCALL("exec()", error);
    }
    _exit(1);
  }

  // Parent process
  KJ_SYSCALL(close(pipeFds[1])); // Close write end

  auto outputStream = ioContext.lowLevelProvider->wrapInputFd(pipeFds[0]);

  capnp::ReaderOptions options{.traversalLimitInWords = 1 << 30};
  auto promise =
      capnp::tryReadMessage(*outputStream, options)
          .then(
              [child](kj::Maybe<kj::Own<capnp::MessageReader>>
                          &&maybeReader) mutable -> RunResult {
                int status;
                KJ_SYSCALL(waitpid(child, &status, 0));

                if (WIFSIGNALED(status)) {
                  KJ_LOG(
                      ERROR, "Process terminated by signal:", WTERMSIG(status));
                  return RunResult{.status = Status::EXECUTION_ERROR};
                }

                int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                if (exitCode != 0) {
                  KJ_LOG(ERROR, "Process failed with exit code", exitCode);
                } else {
                  KJ_LOG(INFO, "Process completed successfully");
                }

                return RunResult{
                    .status = Status::SUCCESS,
                    .exitCode = exitCode,
                    .maybeReader = kj::mv(maybeReader)};
              });

  return promise.attach(kj::mv(outputStream));
}

} // namespace capnp_ls
