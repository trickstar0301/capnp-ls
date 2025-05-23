// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "logger.h"
#include "lsp_message_handler.h"
#include "server_context.h"
#include "stdin_reader.h"
#include "stdout_writer.h"
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/debug.h>
#include <signal.h>
#include <unistd.h>

namespace capnp_ls {

int run() {
  kj::_::Debug::setLogLevel(kj::LogSeverity::WARNING);
  kj::AsyncIoContext ioContext = kj::setupAsyncIo();

  auto paf = kj::newPromiseAndFulfiller<void>();
  ServerContext context(ioContext, kj::mv(paf.fulfiller));

  kj::UnixEventPort::captureSignal(SIGINT);
  kj::UnixEventPort::captureSignal(SIGTERM);
  signal(SIGPIPE, SIG_IGN);
  auto signalPromise =
      ioContext.unixEventPort.onSignal(SIGINT)
          .then([&context](siginfo_t info) {
            KJ_LOG(
                INFO,
                "Received signal SIGINT, initiating shutdown...",
                info.si_signo);
            context.shutdown();
          })
          .exclusiveJoin(ioContext.unixEventPort.onSignal(SIGTERM).then(
              [&context](siginfo_t info) {
                KJ_LOG(
                    INFO,
                    "Received signal SIGTERM, initiating shutdown...",
                    info.si_signo);
                context.shutdown();
              }));

  auto stdout_stream = ioContext.lowLevelProvider->wrapOutputFd(STDOUT_FILENO);
  StdoutWriter stdout_writer(kj::mv(stdout_stream));
  LspLogger logger(stdout_writer);

  auto handler = kj::heap<LspMessageHandler>(context, stdout_writer);

  auto stdin_stream = ioContext.lowLevelProvider->wrapInputFd(STDIN_FILENO);
  StdinReader stdin_reader(kj::mv(stdin_stream), *handler);

  paf.promise.exclusiveJoin(kj::mv(signalPromise)).wait(ioContext.waitScope);

  KJ_LOG(INFO, "Server shutdown complete");
  return 0;
}

} // namespace capnp_ls

int main(int argc, char *argv[]) {
  return capnp_ls::run();
}