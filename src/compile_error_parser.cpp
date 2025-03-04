// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "compile_error_parser.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <regex>

namespace capnp_ls {

int CompileErrorParser::parse(
    kj::StringPtr fileName,
    kj::StringPtr errorText,
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap) {
  try {
    static const std::regex errorPattern(
        R"(\s*((?:\w:(?:\/|\\))?[^:]+):(\d+)(?:-(\d+))?(?::(\d+)(?:-(\d+))?)?:\s*([^:]*):\s*(.*)\s*)");
    bool foundAny = false;

    // Process error text line by line
    const char *start = errorText.begin();
    const char *end = errorText.end();
    const char *lineStart = start;

    while (lineStart < end) {
      // Find next newline
      const char *lineEnd = lineStart;
      while (lineEnd < end && *lineEnd != '\n') {
        ++lineEnd;
      }

      // Process the line if it's not empty
      if (lineEnd > lineStart) {
        // Create a NUL-terminated string for regex matching
        kj::String lineStr = kj::heapString(lineStart, lineEnd - lineStart);

        std::cmatch match;
        if (std::regex_match(lineStr.cStr(), match, errorPattern)) {
          kj::String file = kj::heapString(match[1].first, match[1].length());

          if (file == fileName) {
            foundAny = true;

            uint32_t rowStart = atoi(match[2].first) - 1; // 0-based
            uint32_t rowEnd =
                match[3].matched ? atoi(match[3].first) - 1 : rowStart;

            uint32_t colStart = match[4].matched ? atoi(match[4].first) - 1 : 0;
            uint32_t colEnd =
                match[5].matched ? atoi(match[5].first) - 1 : colStart;

            kj::String type = kj::heapString(match[6].first, match[6].length());
            kj::String message =
                kj::heapString(match[7].first, match[7].length());

            addDiagnostic(
                kj::mv(file),
                rowStart,
                rowEnd,
                colStart,
                colEnd,
                kj::mv(type),
                kj::mv(message),
                diagnosticMap);
          }
        }
      }

      // Move to next line
      lineStart = lineEnd < end ? lineEnd + 1 : end;
    }

    return foundAny ? 0 : 1;
  } catch (kj::Exception &e) {
    KJ_LOG(ERROR, "Error parsing compiler output", e.getDescription());
    return 1;
  }
}

void CompileErrorParser::addDiagnostic(
    const kj::String &file,
    uint32_t rowStart,
    uint32_t rowEnd,
    uint32_t colStart,
    uint32_t colEnd,
    const kj::String &type,
    const kj::String &message,
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap) {
  Diagnostic diagnostic;
  diagnostic.range.start = {rowStart, colStart};
  diagnostic.range.end = {rowEnd, colEnd};
  diagnostic.severity = DiagnosticSeverity::Error;
  diagnostic.message = kj::str(message);
  diagnostic.source = kj::heapString("capnp-compiler");

  auto &diagnostics = diagnosticMap.findOrCreate(
      file, [&]() -> kj::HashMap<kj::String, kj::Vector<Diagnostic>>::Entry {
        return {kj::heapString(file), kj::Vector<Diagnostic>()};
      });
  diagnostics.add(kj::mv(diagnostic));
}

} // namespace capnp_ls
