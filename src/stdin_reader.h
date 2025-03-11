// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "lsp_message_handler.h"
#include <kj/async-io.h>
#include <kj/debug.h>
#include <kj/io.h>

namespace capnp_ls {
class StdinReader : public kj::TaskSet::ErrorHandler {
public:
  static constexpr size_t BUFFER_SIZE = 1 << 20; // 1MB
  explicit StdinReader(
      kj::Own<kj::AsyncInputStream> input,
      LspMessageHandler &handler)
      : tasks(*this), input(kj::mv(input)), handler(handler), currentPos(0) {
    buffer = new char[BUFFER_SIZE];
    tasks.add(monitorStdin());
  }

  ~StdinReader() {
    delete[] buffer;
  }

private:
  kj::Promise<void> monitorStdin();
  void taskFailed(kj::Exception &&exception) override;
  kj::TaskSet tasks;
  kj::Own<kj::AsyncInputStream> input;
  LspMessageHandler &handler;
  char *buffer;
  size_t currentPos;
};
} // namespace capnp_ls