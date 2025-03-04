// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "stdout_writer.h"

namespace capnp_ls {
kj::Promise<void> StdoutWriter::write(kj::StringPtr message) {
  return output->write(message.cStr(), message.size());
}
} // namespace capnp_ls