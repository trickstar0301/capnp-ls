// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include <kj/debug.h>

#include <cstdint>

#include "utils.h"

namespace capnp_ls {

namespace {

// Helper to convert hex digit character to value. Returns -1 if not a hex digit.
int hexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Helper to convert byte value to uppercase hex character pair.
void byteToHex(std::uint8_t value, char &high, char &low) {
  const char hexTable[] = "0123456789ABCDEF";
  high = hexTable[value >> 4];
  low = hexTable[value & 0x0F];
}

} // namespace

kj::String uriToPath(const kj::StringPtr uri) {
  if (!uri.startsWith("file://")) {
    KJ_LOG(ERROR, "URI must start with 'file://'", uri);
    return kj::str(uri);
  }

  auto path = uri.slice(7);

  // Percent-decode the path
  kj::Vector<char> decoded;
  size_t i = 0;
  while (i < path.size()) {
    if (path[i] == '%' && i + 2 < path.size()) {
      // Check if the next two characters are valid hex digits
      int high = hexDigitValue(path[i + 1]);
      int low = hexDigitValue(path[i + 2]);
      if (high >= 0 && low >= 0) {
        // Valid hex sequence: decode it
        std::uint8_t decodedByte = (high << 4) | low;
        decoded.add(decodedByte);
        i += 3;
      } else {
        // Invalid hex sequence: keep the '%' literally
        decoded.add('%');
        i += 1;
      }
    } else {
      // Regular character: copy it
      decoded.add(path[i]);
      i += 1;
    }
  }

  return kj::heapString(decoded.begin(), decoded.size());
}

kj::String pathToUri(const kj::StringPtr path) {
  // Percent-encode the path
  kj::Vector<char> encoded;

  // Add the file:// prefix
  encoded.addAll(kj::StringPtr("file://"));

  // Encode path bytes: keep unreserved chars [A-Za-z0-9-._~] and '/' as-is
  for (size_t i = 0; i < path.size(); i++) {
    std::uint8_t pathByte = static_cast<std::uint8_t>(path[i]);
    if ((pathByte >= 'A' && pathByte <= 'Z') ||
        (pathByte >= 'a' && pathByte <= 'z') ||
        (pathByte >= '0' && pathByte <= '9') ||
        pathByte == '-' || pathByte == '.' || pathByte == '_' || pathByte == '~' || pathByte == '/') {
      encoded.add(pathByte);
    } else {
      // Percent-encode this byte
      encoded.add('%');
      char high, low;
      byteToHex(pathByte, high, low);
      encoded.add(high);
      encoded.add(low);
    }
  }

  return kj::heapString(encoded.begin(), encoded.size());
}

} // namespace capnp_ls
