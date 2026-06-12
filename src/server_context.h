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
  ServerContext(kj::Own<kj::PromiseFulfiller<void>> fulfiller)
      : exitFulfiller(kj::mv(fulfiller)) {}

  void shutdown() {
    KJ_LOG(INFO, "Shutting down server...");
    exitFulfiller->fulfill();
  }

private:
  kj::Own<kj::PromiseFulfiller<void>> exitFulfiller;
};
} // namespace capnp_ls