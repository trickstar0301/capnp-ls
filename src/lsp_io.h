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
class LspIO : public kj::TaskSet::ErrorHandler {
public:
  static constexpr size_t BUFFER_SIZE = 1 << 16; // 64KB
  explicit LspIO(
      kj::Own<kj::AsyncInputStream> input,
      kj::Own<kj::AsyncOutputStream> output,
      LspMessageHandler &handler)
      : tasks(*this), input(kj::mv(input)), output(kj::mv(output)),
        handler(handler), currentPos(0) {
    buffer = new char[BUFFER_SIZE];
    tasks.add(monitorStdin());
  }

  ~LspIO() {
    delete[] buffer;
  }

private:
  kj::Promise<void> monitorStdin();
  void taskFailed(kj::Exception &&exception) override;
  kj::Promise<void> write(kj::StringPtr message);
  kj::TaskSet tasks;
  kj::Own<kj::AsyncInputStream> input;
  kj::Own<kj::AsyncOutputStream> output;
  LspMessageHandler &handler;
  char *buffer;
  size_t currentPos;
};
} // namespace capnp_ls