// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include <kj/debug.h>

#include "utils.h"

namespace capnp_ls {
kj::String uriToPath(const kj::StringPtr uri) {
  if (!uri.startsWith("file://")) {
    KJ_LOG(ERROR, "URI must start with 'file://'", uri);
    return kj::str(uri);
  }

  auto path = uri.slice(7);

  return kj::heapString(path);
}
} // namespace capnp_ls