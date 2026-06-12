// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "lsp_types.h"
#include <kj/debug.h>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

} // namespace

int main() {
  // Test containsPosition: point inside a multi-line range
  {
    capnp_ls::Range range{
        {1, 0},   // start at line 1, char 0
        {3, 5}    // end at line 3, char 5
    };
    require(
        capnp_ls::containsPosition(range, 2, 3),
        "point inside range should be contained");
  }

  // Test containsPosition: point before start
  {
    capnp_ls::Range range{
        {1, 0},
        {3, 5}
    };
    require(
        !capnp_ls::containsPosition(range, 0, 5),
        "point before range start line should not be contained");
  }

  // Test containsPosition: point after end
  {
    capnp_ls::Range range{
        {1, 0},
        {3, 5}
    };
    require(
        !capnp_ls::containsPosition(range, 4, 0),
        "point after range end line should not be contained");
  }

  // Test containsPosition: same line as start, before start character
  {
    capnp_ls::Range range{
        {1, 5},
        {3, 5}
    };
    require(
        !capnp_ls::containsPosition(range, 1, 4),
        "point before start character on same line should not be contained");
  }

  // Test containsPosition: same line as start, at start character (inclusive)
  {
    capnp_ls::Range range{
        {1, 5},
        {3, 5}
    };
    require(
        capnp_ls::containsPosition(range, 1, 5),
        "point at start character on same line should be contained (inclusive)");
  }

  // Test containsPosition: same line as end, at end character (inclusive)
  {
    capnp_ls::Range range{
        {1, 0},
        {3, 5}
    };
    require(
        capnp_ls::containsPosition(range, 3, 5),
        "point at end character on same line should be contained (inclusive)");
  }

  // Test containsPosition: same line as end, after end character
  {
    capnp_ls::Range range{
        {1, 0},
        {3, 5}
    };
    require(
        !capnp_ls::containsPosition(range, 3, 6),
        "point after end character on same line should not be contained");
  }

  // Test isTighterRange: fewer lines wins
  {
    capnp_ls::Range candidate{{1, 0}, {2, 5}};  // 1 line span
    capnp_ls::Range best{{1, 0}, {3, 5}};       // 2 line span
    require(
        capnp_ls::isTighterRange(candidate, best),
        "range with fewer lines should be tighter");
  }

  // Test isTighterRange: more lines loses
  {
    capnp_ls::Range candidate{{1, 0}, {4, 5}};  // 3 line span
    capnp_ls::Range best{{1, 0}, {2, 5}};       // 1 line span
    require(
        !capnp_ls::isTighterRange(candidate, best),
        "range with more lines should not be tighter");
  }

  // Test isTighterRange: equal lines, smaller char span wins
  {
    capnp_ls::Range candidate{{1, 0}, {1, 3}};  // 3 char span
    capnp_ls::Range best{{1, 0}, {1, 5}};       // 5 char span
    require(
        capnp_ls::isTighterRange(candidate, best),
        "range with fewer chars on same line span should be tighter");
  }

  // Test isTighterRange: equal lines, larger char span loses
  {
    capnp_ls::Range candidate{{1, 0}, {1, 7}};  // 7 char span
    capnp_ls::Range best{{1, 0}, {1, 5}};       // 5 char span
    require(
        !capnp_ls::isTighterRange(candidate, best),
        "range with more chars on same line span should not be tighter");
  }

  return 0;
}
