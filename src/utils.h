// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <kj/string.h>

namespace capnp_ls {
kj::String uriToPath(const kj::StringPtr uri);
}