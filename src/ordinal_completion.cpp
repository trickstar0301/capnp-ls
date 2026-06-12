// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "ordinal_completion.h"
#include "kj_compat.h"
#include "utils.h"
#include <kj/vector.h>

namespace capnp_ls {

namespace {

// struct/interface/enum bodies own an ordinal numbering space; union and
// group members number within the enclosing struct's space; braces not
// preceded by one of those keywords are treated as opaque.
enum class BlockKind { OWNS_SPACE, SHARES_PARENT, OPAQUE };

struct Block {
  BlockKind kind;
  // Index (into the open-block stack) of the block owning this block's
  // ordinal space: itself for OWNS_SPACE/OPAQUE, an ancestor for
  // SHARES_PARENT.
  size_t spaceIndex;
  kj::Maybe<uint32_t> maxOrdinal;
};

bool isDecimalDigit(char c) {
  return c >= '0' && c <= '9';
}

bool isHexDigit(char c) {
  return isDecimalDigit(c) || (c >= 'a' && c <= 'f') ||
      (c >= 'A' && c <= 'F');
}

bool isIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentifierChar(char c) {
  return isIdentifierStart(c) || isDecimalDigit(c);
}

} // namespace

kj::Maybe<OrdinalCompletion> computeOrdinalCompletion(
    kj::StringPtr text,
    uint32_t line,
    uint32_t character) {
  size_t cursorOffset = 0;
  CAPNP_LS_IF_SOME (offset, byteOffsetForPosition(text, line, character)) {
    cursorOffset = *offset;
  } else {
    return CAPNP_LS_NONE;
  }

  // Only complete right after '@' plus any digits already typed.
  size_t digitsStart = cursorOffset;
  while (digitsStart > 0 && isDecimalDigit(text[digitsStart - 1])) {
    --digitsStart;
  }
  if (digitsStart == 0 || text[digitsStart - 1] != '@') {
    return CAPNP_LS_NONE;
  }
  uint32_t typedDigits = static_cast<uint32_t>(cursorOffset - digitsStart);

  auto makeCompletion = [&](kj::Maybe<uint32_t> maxOrdinal) {
    uint32_t next = 0;
    CAPNP_LS_IF_SOME (value, maxOrdinal) {
      next = *value + 1;
    }
    Range replaceRange{
        {line, character - typedDigits},
        {line, character}};
    return OrdinalCompletion{next, replaceRange};
  };

  enum class LexState { CODE, COMMENT, STRING };
  enum class Pending { NONE, OWNS_SPACE, SHARES_PARENT };

  kj::Vector<Block> stack;
  LexState state = LexState::CODE;
  Pending pending = Pending::NONE;
  bool cursorSeen = false;
  kj::Maybe<size_t> cursorSpace;

  auto snapshotCursor = [&]() {
    cursorSeen = true;
    if (state != LexState::CODE || stack.size() == 0) {
      return;
    }
    size_t space = stack[stack.size() - 1].spaceIndex;
    if (stack[space].kind == BlockKind::OWNS_SPACE) {
      cursorSpace = space;
    }
  };

  size_t i = 0;
  while (i < text.size()) {
    if (!cursorSeen && i >= cursorOffset) {
      snapshotCursor();
    }
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
        state = LexState::STRING;
        ++i;
      } else if (c == ';') {
        pending = Pending::NONE;
        ++i;
      } else if (c == '{') {
        Block block;
        if (pending == Pending::OWNS_SPACE) {
          block.kind = BlockKind::OWNS_SPACE;
          block.spaceIndex = stack.size();
        } else if (pending == Pending::SHARES_PARENT && stack.size() > 0) {
          block.kind = BlockKind::SHARES_PARENT;
          block.spaceIndex = stack[stack.size() - 1].spaceIndex;
        } else {
          block.kind = BlockKind::OPAQUE;
          block.spaceIndex = stack.size();
        }
        stack.add(block);
        pending = Pending::NONE;
        ++i;
      } else if (c == '}') {
        pending = Pending::NONE;
        if (stack.size() > 0) {
          size_t closing = stack.size() - 1;
          CAPNP_LS_IF_SOME (space, cursorSpace) {
            if (*space == closing) {
              return makeCompletion(stack[closing].maxOrdinal);
            }
          }
          stack.removeLast();
        }
        ++i;
      } else if (c == '@') {
        size_t numberStart = i + 1;
        if (numberStart + 1 < text.size() && text[numberStart] == '0' &&
            (text[numberStart + 1] == 'x' || text[numberStart + 1] == 'X')) {
          // 64-bit hex ID (file/type/annotation), not an ordinal.
          i = numberStart + 2;
          while (i < text.size() && isHexDigit(text[i])) {
            ++i;
          }
        } else if (
            numberStart < text.size() && isDecimalDigit(text[numberStart])) {
          uint32_t value = 0;
          size_t end = numberStart;
          while (end < text.size() && isDecimalDigit(text[end])) {
            if (value < 0xffff) {
              value = value * 10 + static_cast<uint32_t>(text[end] - '0');
            }
            ++end;
          }
          // The ordinal under the cursor is the one being completed; it must
          // not count toward the existing maximum.
          bool isTokenBeingTyped = cursorOffset > i && cursorOffset <= end;
          if (!isTokenBeingTyped && stack.size() > 0) {
            Block &owner = stack[stack[stack.size() - 1].spaceIndex];
            CAPNP_LS_IF_SOME (current, owner.maxOrdinal) {
              if (value > *current) {
                owner.maxOrdinal = value;
              }
            } else {
              owner.maxOrdinal = value;
            }
          }
          i = end;
        } else {
          ++i;
        }
      } else if (isIdentifierStart(c)) {
        size_t end = i + 1;
        while (end < text.size() && isIdentifierChar(text[end])) {
          ++end;
        }
        auto word = text.slice(i, end);
        if (word == kj::StringPtr("struct") ||
            word == kj::StringPtr("interface") ||
            word == kj::StringPtr("enum")) {
          pending = Pending::OWNS_SPACE;
        } else if (
            word == kj::StringPtr("union") || word == kj::StringPtr("group")) {
          pending = Pending::SHARES_PARENT;
        }
        i = end;
      } else {
        ++i;
      }
      break;
    }
  }

  if (!cursorSeen) {
    snapshotCursor();
  }
  CAPNP_LS_IF_SOME (space, cursorSpace) {
    // The owning block never closed (file still being edited).
    return makeCompletion(stack[*space].maxOrdinal);
  }
  return CAPNP_LS_NONE;
}

} // namespace capnp_ls
