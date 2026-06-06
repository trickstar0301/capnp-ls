# Copyright (c) 2024 Atsushi Tomida
#
# Licensed under the MIT License.
# See LICENSE file in the project root for full license information.

@0xdba53d6c0e9fe301;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("capnp_ls_test");

struct UsesStandardImport {
  value @0 :Text;
}
