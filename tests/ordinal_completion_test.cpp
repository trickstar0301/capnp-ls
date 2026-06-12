// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "kj_compat.h"
#include "ordinal_completion.h"
#include <kj/debug.h>
#include <string>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

// Internal 1-based position right after the first occurrence of needle.
// ASCII-only inputs: byte columns equal UTF-16 columns.
capnp_ls::Position cursorAfter(kj::StringPtr text, kj::StringPtr needle) {
  std::string haystack(text.cStr(), text.size());
  auto found = haystack.find(std::string(needle.cStr(), needle.size()));
  require(found != std::string::npos, "test needle should exist in text");
  size_t offset = found + needle.size();
  capnp_ls::Position position{1, 1};
  for (size_t i = 0; i < offset; ++i) {
    if (haystack[i] == '\n') {
      ++position.line;
      position.character = 1;
    } else {
      ++position.character;
    }
  }
  return position;
}

void expectNext(
    kj::StringPtr text,
    kj::StringPtr needle,
    uint32_t expected,
    kj::StringPtr message) {
  auto position = cursorAfter(text, needle);
  auto result = capnp_ls::computeOrdinalCompletion(
      text, position.line, position.character);
  CAPNP_LS_IF_SOME (completion, result) {
    require(completion->nextOrdinal == expected, message);
    return;
  }
  KJ_FAIL_REQUIRE(message, "expected a completion, got none");
}

void expectNone(kj::StringPtr text, kj::StringPtr needle, kj::StringPtr message) {
  auto position = cursorAfter(text, needle);
  auto result = capnp_ls::computeOrdinalCompletion(
      text, position.line, position.character);
  CAPNP_LS_IF_SOME (completion, result) {
    (void)completion;
    KJ_FAIL_REQUIRE(message, "expected none, got a completion");
  }
}

} // namespace

int main() {
  // Next ordinal after existing struct fields.
  expectNext(
      R"(@0xdba53d6c0e9fe303;
struct Person {
  name @0 :Text;
  age @1 :UInt32;
  email @
}
)",
      "email @",
      2,
      "struct with @0 and @1 should suggest 2");

  // First field of an empty struct.
  expectNext(
      R"(struct Empty {
  first @
}
)",
      "first @",
      0,
      "empty struct should suggest 0");

  // Nested struct owns its own ordinal space; outer is unaffected by it.
  {
    kj::StringPtr text = R"(struct Outer {
  a @0 :Bool;
  b @1 :Bool;
  c @2 :Bool;
  struct Inner {
    x @0 :Bool;
    y @
  }
  d @
}
)";
    expectNext(text, "y @", 1, "nested struct should number independently");
    expectNext(
        text, "d @", 3, "outer struct should not count nested struct ordinals");
  }

  // Union members share the enclosing struct's ordinal space.
  expectNext(
      R"(struct Shape {
  area @0 :Float64;
  union {
    circle @1 :Float64;
    square @
  }
}
)",
      "square @",
      2,
      "union members should share the struct ordinal space");

  // Group members share the enclosing struct's ordinal space.
  expectNext(
      R"(struct Point {
  x @0 :Int32;
  pos :group {
    y @1 :Int32;
    z @
  }
}
)",
      "z @",
      2,
      "group members should share the struct ordinal space");

  // Field after a union counts the union's ordinals.
  expectNext(
      R"(struct Shape {
  area @0 :Float64;
  union {
    circle @1 :Float64;
    square @2 :Float64;
  }
  label @
}
)",
      "label @",
      3,
      "field after a union should count union ordinals");

  // Enum enumerants get their own space.
  expectNext(
      R"(enum Color {
  red @0;
  green @1;
  blue @
}
)",
      "blue @",
      2,
      "enum should suggest the next enumerant ordinal");

  // Interface methods get their own space.
  expectNext(
      R"(interface Calculator {
  add @0 (a :Int32, b :Int32) -> (result :Int32);
  subtract @
}
)",
      "subtract @",
      1,
      "interface should suggest the next method ordinal");

  // 64-bit hex IDs are not ordinals.
  expectNext(
      R"(@0xdba53d6c0e9fe303;

struct Foo @0xbf5147cbbecf40c1 {
  a @
}
)",
      "a @",
      0,
      "hex type and file IDs should not count as ordinals");

  // Ordinals mentioned in comments do not count.
  expectNext(
      R"(struct S {
  # legacy field was @5
  a @
}
)",
      "a @",
      0,
      "ordinals in comments should not count");

  // Ordinals and braces inside string literals do not count.
  expectNext(
      R"(struct S {
  s @0 :Text = "br { ace @7";
  t @
}
)",
      "t @",
      1,
      "ordinals and braces in strings should not count");

  // Digits already typed are excluded from the scan and covered by the
  // replacement range.
  {
    kj::StringPtr text = R"(struct S {
  a @0 :Bool;
  b @1
}
)";
    auto position = cursorAfter(text, "b @1");
    auto result = capnp_ls::computeOrdinalCompletion(
        text, position.line, position.character);
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          completion->nextOrdinal == 1,
          "ordinal being typed should not count toward the maximum");
      require(
          completion->replaceRange.start.line == position.line &&
              completion->replaceRange.end.line == position.line,
          "replace range should stay on the cursor line");
      require(
          completion->replaceRange.start.character == position.character - 1 &&
              completion->replaceRange.end.character == position.character,
          "replace range should cover the typed digits");
    } else {
      KJ_FAIL_REQUIRE("typed digits should still produce a completion");
    }
  }

  // The replacement range is empty when nothing is typed after '@'.
  {
    kj::StringPtr text = R"(struct S {
  a @
}
)";
    auto position = cursorAfter(text, "a @");
    auto result = capnp_ls::computeOrdinalCompletion(
        text, position.line, position.character);
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          completion->replaceRange.start == completion->replaceRange.end &&
              completion->replaceRange.start.character == position.character,
          "replace range should be empty at the cursor");
    } else {
      KJ_FAIL_REQUIRE("bare '@' should produce a completion");
    }
  }

  // Ordinals declared after the cursor still count.
  expectNext(
      R"(struct S {
  a @1 :Bool;
  b @
  z @0 :Bool;
}
)",
      "b @",
      2,
      "ordinals after the cursor should count");

  // Unclosed struct (mid-edit) still completes.
  expectNext(
      "struct S {\n  a @0 :Bool;\n  b @",
      "b @",
      1,
      "unclosed struct should still complete");

  // UTF-16 column accounting: the emoji counts as two units.
  {
    kj::StringPtr text =
        "struct S {\n  s @0 :Text = \"\xF0\x9F\x98\x80\"; t @\n}\n";
    auto result = capnp_ls::computeOrdinalCompletion(text, 2, 25);
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          completion->nextOrdinal == 1,
          "UTF-16 cursor after an emoji should suggest the next ordinal");
    } else {
      KJ_FAIL_REQUIRE("UTF-16 cursor after an emoji should complete");
    }
  }

  // No completion without an '@' before the cursor.
  expectNone(
      R"(struct S {
  name
}
)",
      "name",
      "cursor without a preceding '@' should not complete");

  // No completion at the top level (file IDs are hex, not ordinals).
  expectNone("@", "@", "top-level '@' should not complete");

  // No completion inside a comment.
  expectNone(
      R"(struct S {
  a @0 :Bool; # note @
}
)",
      "note @",
      "'@' inside a comment should not complete");

  return 0;
}
