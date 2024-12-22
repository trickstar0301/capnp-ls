// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <kj/async-io.h>

namespace capnp_ls {
class StdoutWriter {
public:
  StdoutWriter(kj::Own<kj::AsyncOutputStream> output)
      : output(kj::mv(output)) {}

  kj::Promise<void> write(kj::StringPtr message);

private:
  kj::Own<kj::AsyncOutputStream> output;
};
} // namespace capnp_ls
