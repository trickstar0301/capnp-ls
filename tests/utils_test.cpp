// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "utils.h"
#include <kj/debug.h>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

} // namespace

int main() {
  // Test uriToPath: basic file URI without special characters
  {
    auto result = capnp_ls::uriToPath("file:///tmp/a.capnp");
    require(result == "/tmp/a.capnp", "uriToPath basic case");
  }

  // Test uriToPath: percent-decoded space (%20)
  {
    auto result = capnp_ls::uriToPath("file:///tmp/my%20dir/a.capnp");
    require(result == "/tmp/my dir/a.capnp", "uriToPath with %20 space");
  }

  // Test uriToPath: percent-decoded colon (%3A)
  {
    auto result = capnp_ls::uriToPath("file:///c%3A/x.capnp");
    require(result == "/c:/x.capnp", "uriToPath with %3A colon");
  }

  // Test uriToPath: lowercase hex digits
  {
    auto result = capnp_ls::uriToPath("file:///a%2fb");
    require(result == "/a/b", "uriToPath with lowercase hex %2f");
  }

  // Test uriToPath: invalid/truncated sequence (incomplete %2)
  {
    auto result = capnp_ls::uriToPath("file:///a%2");
    require(result == "/a%2", "uriToPath with truncated %2");
  }

  // Test uriToPath: invalid hex sequence (%zz)
  {
    auto result = capnp_ls::uriToPath("file:///a%zz");
    require(result == "/a%zz", "uriToPath with invalid hex %zz");
  }

  // Test uriToPath: non-file URI returns input unchanged
  {
    auto result = capnp_ls::uriToPath("untitled:abc");
    require(result == "untitled:abc", "uriToPath non-file URI unchanged");
  }

  // Test pathToUri: basic path without special characters
  {
    auto result = capnp_ls::pathToUri("/tmp/a.capnp");
    require(result == "file:///tmp/a.capnp", "pathToUri basic case");
  }

  // Test pathToUri: path with space
  {
    auto result = capnp_ls::pathToUri("/tmp/my dir/a.capnp");
    require(result == "file:///tmp/my%20dir/a.capnp", "pathToUri with space");
  }

  // Test pathToUri: round trip with UTF-8 (UTF-8 bytes get %-encoded)
  {
    auto path = "/tmp/my dir/ä.capnp";
    auto uri = capnp_ls::pathToUri(path);
    auto decoded = capnp_ls::uriToPath(uri);
    require(decoded == path, "pathToUri/uriToPath round trip with UTF-8");
  }

  return 0;
}
