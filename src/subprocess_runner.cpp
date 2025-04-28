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
  int errPipe[2];
  KJ_SYSCALL(pipe(pipeFds));
  KJ_SYSCALL(pipe(errPipe));

  pid_t child;
  KJ_SYSCALL(child = fork());

  if (child == 0) {
    // Child process
    // To avoid logging from child process
    kj::_::Debug::setLogLevel(kj::LogSeverity::FATAL);

    KJ_SYSCALL(close(pipeFds[0]));
    KJ_SYSCALL(dup2(pipeFds[1], STDOUT_FILENO));
    KJ_SYSCALL(close(pipeFds[1]));

    KJ_SYSCALL(close(errPipe[0]));
    KJ_SYSCALL(dup2(errPipe[1], STDERR_FILENO));
    KJ_SYSCALL(close(errPipe[1]));

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
  KJ_SYSCALL(close(errPipe[1])); // Close write end

  auto outputStream = ioContext.lowLevelProvider->wrapInputFd(pipeFds[0]);
  auto errorStream = ioContext.lowLevelProvider->wrapInputFd(errPipe[0]);

  capnp::ReaderOptions options{.traversalLimitInWords = 1 << 30};
  kj::Promise<RunResult> outputPromise = kj::Promise<RunResult>(RunResult{
      .status = Status::EXECUTION_ERROR,
      .exitCode = -1,
      .errorText = kj::str("Failed to read output")});
  if (params.isCapnpMessageOutput) {
    outputPromise =
        capnp::tryReadMessage(*outputStream, options)
            .then(
                [child](kj::Maybe<kj::Own<capnp::MessageReader>>
                            &&maybeReader) mutable -> RunResult {
                  return RunResult{
                      .exitCode = 0, .maybeReader = kj::mv(maybeReader)};
                })
            .attach(kj::mv(outputStream));
  } else {
    outputPromise = outputStream->readAllText()
                        .then([child](kj::StringPtr text) {
                          return RunResult{.textOutput = kj::str(text)};
                        })
                        .attach(kj::mv(outputStream));
  }

  auto errorPromise = errorStream->readAllText()
                          .then([child](kj::StringPtr errorText) {
                            return RunResult{.errorText = kj::str(errorText)};
                          })
                          .attach(kj::mv(errorStream));

  auto builder = kj::heapArrayBuilder<kj::Promise<RunResult>>(2);
  builder.add(kj::mv(outputPromise));
  builder.add(kj::mv(errorPromise));
  return kj::joinPromises(builder.finish())
      .then([child](kj::Array<RunResult> &&outputs) {
        int status;
        KJ_SYSCALL(waitpid(child, &status, 0));
        return RunResult{
            .status = Status::SUCCESS,
            .exitCode = WEXITSTATUS(status),
            .maybeReader = kj::mv(outputs[0].maybeReader),
            .textOutput = kj::mv(outputs[0].textOutput),
            .errorText = kj::mv(outputs[1].errorText),
        };
      });

  // return outputPromise.exclusiveJoin(kj::mv(errorPromise));
}

} // namespace capnp_ls
