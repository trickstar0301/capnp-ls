// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <iostream>
#include <kj/map.h>
#include <kj/string.h>
#include <cstdint>

namespace capnp_ls {

// LSP protocol header constants
static constexpr const char *LSP_CONTENT_LENGTH_HEADER = "Content-Length: ";
static constexpr size_t LSP_CONTENT_LENGTH_HEADER_SIZE = 16;
static constexpr const char *LSP_HEADER_DELIMITER = "\r\n\r\n";
static constexpr size_t LSP_HEADER_DELIMITER_SIZE = 4;
static constexpr int LSP_CONTENT_LENGTH_RADIX = 10; // base 10
static constexpr const char *LSP_JSON_RPC_VERSION = "2.0";

// LSP JSON message fields
constexpr const char LSP_METHOD[] = "method";
constexpr const char LSP_PARAMS[] = "params";
constexpr const char LSP_ID[] = "id";
constexpr const char LSP_JSONRPC[] = "jsonrpc";
constexpr const char LSP_RESULT[] = "result";

#define LSP_FOR_EACH_METHOD(MACRO)                                             \
  MACRO(INITIALIZE, "initialize")                                              \
  MACRO(SHUTDOWN, "shutdown")                                                  \
  MACRO(DEFINITION, "textDocument/definition")                                 \
  MACRO(DID_OPEN, "textDocument/didOpen")                                      \
  MACRO(DID_CHANGE_WATCHED_FILES, "workspace/didChangeWatchedFiles")           \
  MACRO(DID_SAVE, "textDocument/didSave")                                      \
  MACRO(DID_CHANGE, "textDocument/didChange")                                  \
  MACRO(INITIALIZED, "initialized")                                            \
  MACRO(SET_TRACE, "$/setTrace")                                               \
  MACRO(CANCEL_REQUEST, "$/cancelRequest")

enum class LspMethod {
#define DECLARE_METHOD(id, name) id,
  LSP_FOR_EACH_METHOD(DECLARE_METHOD)
#undef DECLARE_METHOD
};

kj::StringPtr KJ_STRINGIFY(LspMethod method);
kj::Maybe<LspMethod> tryParseLspMethod(kj::StringPtr name);

struct Position {
  uint32_t line;
  uint32_t character;

  bool operator==(const Position &other) const {
    return line == other.line && character == other.character;
  }
  unsigned int hashCode() const {
    return kj::hashCode(line, character);
  }
};

struct Range {
  Position start;
  Position end;

  bool operator==(const Range &other) const {
    return start == other.start && end == other.end;
  }
  unsigned int hashCode() const {
    return kj::hashCode(start, end);
  }
};

struct Location {
  kj::String uri;
  Range range;
};

enum class DiagnosticSeverity {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4
};

struct Diagnostic {
  Range range;
  DiagnosticSeverity severity;
  kj::String message;
  kj::String source; // "capnp-compiler"
};

struct CompileError {
  kj::String file;
  uint32_t rowStart;
  uint32_t rowEnd;
  uint32_t colStart;
  uint32_t colEnd;
  kj::String type;
  kj::String message;
};
} // namespace capnp_ls