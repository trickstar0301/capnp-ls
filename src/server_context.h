// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <kj/async-io.h>
#include <kj/debug.h>

namespace capnp_ls {

class ServerContext {
public:
  ServerContext(
      kj::AsyncIoContext &context,
      kj::Own<kj::PromiseFulfiller<void>> fulfiller)
      : ioContext(context), exitFulfiller(kj::mv(fulfiller)) {}

  void shutdown() {
    KJ_LOG(INFO, "Shutting down server...");
    exitFulfiller->fulfill();
  }
  kj::AsyncIoContext &getIoContext() {
    return ioContext;
  }

private:
  kj::AsyncIoContext &ioContext;
  kj::Own<kj::PromiseFulfiller<void>> exitFulfiller;
};
} // namespace capnp_ls