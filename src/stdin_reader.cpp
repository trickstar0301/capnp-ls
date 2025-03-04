// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "stdin_reader.h"
#include <cstdlib>

namespace capnp_ls {
struct ParsedMessage {
  size_t processedSize;
  kj::Maybe<kj::StringPtr> content;
};

ParsedMessage
parseNextMessage(const char *buffer, size_t currentPos, size_t processedPos) {
  // Find the end of the header
  const char *headerEnd = strstr(buffer + processedPos, LSP_HEADER_DELIMITER);
  if (!headerEnd) {
    return {processedPos, nullptr};
  }

  // Parse Content-Length
  const char *contentLengthStr =
      strstr(buffer + processedPos, LSP_CONTENT_LENGTH_HEADER);
  if (!contentLengthStr) {
    return {
        static_cast<size_t>(headerEnd - buffer + LSP_HEADER_DELIMITER_SIZE),
        nullptr};
  }

  size_t contentLength = strtoul(
      contentLengthStr + LSP_CONTENT_LENGTH_HEADER_SIZE,
      nullptr,
      LSP_CONTENT_LENGTH_RADIX);
  size_t headerSize =
      (headerEnd - buffer) + LSP_HEADER_DELIMITER_SIZE - processedPos;
  size_t totalMessageSize = headerSize + contentLength;

  if (currentPos - processedPos < totalMessageSize) {
    return {processedPos, nullptr};
  }

  return {
      processedPos + totalMessageSize,
      kj::StringPtr(buffer + processedPos, totalMessageSize)};
}

kj::Promise<void> StdinReader::monitorStdin() {
  return input->tryRead(buffer + currentPos, 0, BUFFER_SIZE - currentPos - 1)
      .then([this](size_t n) {
        if (n == 0) {
          KJ_LOG(INFO, "EOF detected on stdin");
          tasks.add(handler.handleMessage(kj::Maybe<kj::StringPtr>(nullptr)));
          return kj::Promise<void>(kj::READY_NOW);
        }

        currentPos += n;
        buffer[currentPos] = '\0';

        size_t processedPos = 0;
        while (processedPos < currentPos) {
          auto result = parseNextMessage(buffer, currentPos, processedPos);
          processedPos = result.processedSize;

          KJ_IF_MAYBE (content, result.content) {
            tasks.add(handler.handleMessage(*content));
          } else {
            break;
          }
        }

        if (processedPos > 0 && processedPos < currentPos) {
          memmove(buffer, buffer + processedPos, currentPos - processedPos);
          currentPos -= processedPos;
        } else if (processedPos == currentPos) {
          currentPos = 0;
        }
        return monitorStdin();
      });
}

void StdinReader::taskFailed(kj::Exception &&exception) {
  KJ_LOG(ERROR, "task failed", exception.getDescription());
}
} // namespace capnp_ls