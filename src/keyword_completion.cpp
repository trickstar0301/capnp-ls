// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "keyword_completion.h"
#include "kj_compat.h"
#include "utils.h"
#include <kj/vector.h>

namespace capnp_ls {

namespace {

enum class BlockKind { STRUCT, INTERFACE, ENUM, UNION_GROUP, OPAQUE };

// Keywords that may start a declaration at file scope or in an interface
// body. Struct bodies additionally allow an unnamed "union"; union and group
// bodies allow only a nested "union"; enum bodies hold only enumerants.
const kj::StringPtr DECLARATION_KEYWORDS[] = {
    "struct",
    "interface",
    "enum",
    "const",
    "using",
    "annotation"};
const kj::StringPtr STRUCT_KEYWORDS[] = {
    "struct",
    "interface",
    "enum",
    "union",
    "const",
    "using",
    "annotation"};
const kj::StringPtr UNION_GROUP_KEYWORDS[] = {"union"};

template <size_t size>
kj::ArrayPtr<const kj::StringPtr> keywordList(
    const kj::StringPtr (&list)[size]) {
  return kj::arrayPtr(list, size);
}

bool isDecimalDigit(char c) {
  return c >= '0' && c <= '9';
}

bool isIdentifierChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
      isDecimalDigit(c);
}

bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

} // namespace

kj::Maybe<KeywordCompletion> computeKeywordCompletion(
    kj::StringPtr text,
    uint32_t line,
    uint32_t character) {
  size_t cursorOffset = 0;
  CAPNP_LS_IF_SOME (offset, byteOffsetForPosition(text, line, character)) {
    cursorOffset = *offset;
  } else {
    return CAPNP_LS_NONE;
  }

  enum class LexState { CODE, COMMENT, STRING };

  kj::Vector<BlockKind> stack;
  LexState state = LexState::CODE;
  kj::Maybe<BlockKind> pending;
  size_t tokenCount = 0;
  size_t firstTokenStart = 0;

  size_t i = 0;
  while (i < cursorOffset) {
    char c = text[i];
    switch (state) {
    case LexState::COMMENT:
      if (c == '\n') {
        state = LexState::CODE;
      }
      ++i;
      break;
    case LexState::STRING:
      if (c == '\\') {
        i += 2;
      } else {
        if (c == '"' || c == '\n') {
          state = LexState::CODE;
        }
        ++i;
      }
      break;
    case LexState::CODE:
      if (c == '#') {
        state = LexState::COMMENT;
        ++i;
      } else if (c == '"') {
        if (tokenCount == 0) {
          firstTokenStart = i;
        }
        ++tokenCount;
        state = LexState::STRING;
        ++i;
      } else if (c == ';') {
        tokenCount = 0;
        pending = CAPNP_LS_NONE;
        ++i;
      } else if (c == '{') {
        BlockKind kind = BlockKind::OPAQUE;
        CAPNP_LS_IF_SOME (pendingKind, pending) {
          kind = *pendingKind;
        }
        stack.add(kind);
        pending = CAPNP_LS_NONE;
        tokenCount = 0;
        ++i;
      } else if (c == '}') {
        if (stack.size() > 0) {
          stack.removeLast();
        }
        pending = CAPNP_LS_NONE;
        tokenCount = 0;
        ++i;
      } else if (isIdentifierChar(c)) {
        if (tokenCount == 0) {
          firstTokenStart = i;
        }
        ++tokenCount;
        size_t end = i + 1;
        while (end < text.size() && isIdentifierChar(text[end])) {
          ++end;
        }
        auto word = text.slice(i, end);
        if (word == kj::StringPtr("struct")) {
          pending = BlockKind::STRUCT;
        } else if (word == kj::StringPtr("interface")) {
          pending = BlockKind::INTERFACE;
        } else if (word == kj::StringPtr("enum")) {
          pending = BlockKind::ENUM;
        } else if (
            word == kj::StringPtr("union") || word == kj::StringPtr("group")) {
          pending = BlockKind::UNION_GROUP;
        }
        i = end;
      } else if (isWhitespace(c)) {
        ++i;
      } else {
        if (tokenCount == 0) {
          firstTokenStart = i;
        }
        ++tokenCount;
        ++i;
      }
      break;
    }
  }

  if (state != LexState::CODE) {
    return CAPNP_LS_NONE;
  }

  size_t prefixStart = cursorOffset;
  while (prefixStart > 0 && isIdentifierChar(text[prefixStart - 1])) {
    --prefixStart;
  }
  if (prefixStart > 0 && text[prefixStart - 1] == '@') {
    return CAPNP_LS_NONE; // an ordinal position
  }
  bool atStatementStart = prefixStart == cursorOffset
      ? tokenCount == 0
      : tokenCount == 1 && firstTokenStart == prefixStart;
  if (!atStatementStart) {
    return CAPNP_LS_NONE;
  }

  kj::ArrayPtr<const kj::StringPtr> keywords;
  if (stack.size() == 0) {
    keywords = keywordList(DECLARATION_KEYWORDS);
  } else {
    switch (stack[stack.size() - 1]) {
    case BlockKind::STRUCT:
      keywords = keywordList(STRUCT_KEYWORDS);
      break;
    case BlockKind::INTERFACE:
      keywords = keywordList(DECLARATION_KEYWORDS);
      break;
    case BlockKind::UNION_GROUP:
      keywords = keywordList(UNION_GROUP_KEYWORDS);
      break;
    case BlockKind::ENUM:
    case BlockKind::OPAQUE:
      return CAPNP_LS_NONE;
    }
  }

  uint32_t prefixLength = static_cast<uint32_t>(cursorOffset - prefixStart);
  Range replaceRange{{line, character - prefixLength}, {line, character}};
  return KeywordCompletion{keywords, replaceRange};
}

} // namespace capnp_ls
