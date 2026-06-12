// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "keyword_completion.h"
#include "kj_compat.h"
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

kj::Maybe<capnp_ls::KeywordCompletion>
compute(kj::StringPtr text, kj::StringPtr needle) {
  auto position = cursorAfter(text, needle);
  return capnp_ls::computeKeywordCompletion(
      text, position.line, position.character);
}

bool hasKeyword(
    kj::ArrayPtr<const kj::StringPtr> keywords,
    kj::StringPtr expected) {
  for (auto &keyword : keywords) {
    if (keyword == expected) {
      return true;
    }
  }
  return false;
}

void expectKeywords(
    kj::StringPtr text,
    kj::StringPtr needle,
    size_t expectedCount,
    kj::StringPtr mustContain,
    kj::StringPtr message) {
  auto result = compute(text, needle);
  CAPNP_LS_IF_SOME (completion, result) {
    require(completion->keywords.size() == expectedCount, message);
    require(hasKeyword(completion->keywords, mustContain), message);
    return;
  }
  KJ_FAIL_REQUIRE(message, "expected keywords, got none");
}

void expectNone(kj::StringPtr text, kj::StringPtr needle, kj::StringPtr message) {
  auto result = compute(text, needle);
  CAPNP_LS_IF_SOME (completion, result) {
    (void)completion;
    KJ_FAIL_REQUIRE(message, "expected none, got keywords");
  }
}

} // namespace

int main() {
  // File scope offers declaration keywords but not union.
  {
    kj::StringPtr text = R"(@0xdba53d6c0e9fe303;

stru
)";
    expectKeywords(
        text, "stru", 6, "struct", "file scope should offer declaration keywords");
    auto result = compute(text, "stru");
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          !hasKeyword(completion->keywords, "union"),
          "file scope should not offer union");
      require(
          hasKeyword(completion->keywords, "using") &&
              hasKeyword(completion->keywords, "annotation"),
          "file scope should offer using and annotation");
      auto position = cursorAfter(text, "stru");
      require(
          completion->replaceRange.start.line == position.line &&
              completion->replaceRange.start.character ==
                  position.character - 4 &&
              completion->replaceRange.end == position,
          "replace range should cover the typed prefix");
    } else {
      KJ_FAIL_REQUIRE("file scope should offer keywords");
    }
  }

  // Struct bodies additionally offer union.
  expectKeywords(
      R"(struct Person {
  name @0 :Text;
  un
}
)",
      "un",
      7,
      "union",
      "struct body should offer declaration keywords plus union");

  // Interface bodies offer declaration keywords but not union.
  {
    kj::StringPtr text = R"(interface Calc {
  add @0 () -> ();
  st
}
)";
    expectKeywords(
        text, "st", 6, "struct", "interface body should offer declaration keywords");
    auto result = compute(text, "st");
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          !hasKeyword(completion->keywords, "union"),
          "interface body should not offer union");
    }
  }

  // Enum bodies hold only enumerants.
  expectNone(
      R"(enum Color {
  red @0;
  gr
}
)",
      "gr",
      "enum body should not offer keywords");

  // Unnamed union bodies offer only a nested union.
  expectKeywords(
      R"(struct S {
  union {
    un
  }
}
)",
      "    un",
      1,
      "union",
      "union body should offer only union");

  // Group bodies offer only a nested union.
  expectKeywords(
      R"(struct S {
  g :group {
    un
  }
}
)",
      "un",
      1,
      "union",
      "group body should offer only union");

  // A second statement on the same line still starts a statement.
  expectKeywords(
      R"(struct S {
  a @0 :Bool; un
}
)",
      "un",
      7,
      "union",
      "statement after a ';' on the same line should offer keywords");

  // Manual invocation with no prefix at a statement start.
  {
    kj::StringPtr text = "struct S {\n  \n}\n";
    auto result = capnp_ls::computeKeywordCompletion(text, 2, 3);
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          completion->keywords.size() == 7,
          "empty prefix at a statement start should offer all keywords");
      require(
          completion->replaceRange.start == completion->replaceRange.end,
          "empty prefix should produce an empty replace range");
    } else {
      KJ_FAIL_REQUIRE("empty prefix at a statement start should complete");
    }
  }

  // Manual invocation in an empty file.
  {
    auto result = capnp_ls::computeKeywordCompletion("", 1, 1);
    CAPNP_LS_IF_SOME (completion, result) {
      require(
          completion->keywords.size() == 6,
          "empty file should offer file-scope keywords");
    } else {
      KJ_FAIL_REQUIRE("empty file should offer keywords");
    }
  }

  // Not the first word of the statement.
  expectNone(
      "const ma\n",
      "ma",
      "second word of a statement should not offer keywords");

  // Type position after ':' is not a keyword position.
  expectNone(
      R"(struct S {
  name :Te
}
)",
      "Te",
      "type position should not offer keywords");

  // Comments and strings never complete.
  expectNone(
      R"(struct S {
  # stru
}
)",
      "# stru",
      "comments should not offer keywords");
  expectNone(
      R"(const s :Text = "stru)",
      "\"stru",
      "strings should not offer keywords");

  // Right after '@' is an ordinal position, not a keyword position.
  expectNone(
      R"(struct S {
  email @
}
)",
      "email @",
      "'@' positions should not offer keywords");

  return 0;
}
