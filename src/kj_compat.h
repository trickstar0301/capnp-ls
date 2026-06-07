// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <capnp/common.h>
#include <kj/common.h>

// Keeps the old KJ_IF_MAYBE pointer-like binding semantics without using the
// deprecated macro name when building against Cap'n Proto v2.
#define CAPNP_LS_IF_SOME(name, exp) if (auto name = ::kj::_::readMaybe(exp))

#if CAPNP_VERSION_MAJOR >= 2
#define CAPNP_LS_NONE kj::none
#else
#define CAPNP_LS_NONE nullptr
#endif
