// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "stdout_writer.h"
#include <capnp/common.h>

namespace capnp_ls {
kj::Promise<void> StdoutWriter::write(kj::StringPtr message) {
#if CAPNP_VERSION_MAJOR >= 2
  auto bytes = kj::arrayPtr(
      reinterpret_cast<const kj::byte *>(message.begin()),
      message.size());
  return output->write(bytes);
#else
  return output->write(message.begin(), message.size());
#endif
}
} // namespace capnp_ls
