// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "json_rpc.h"
#include "kj_compat.h"
#include "lsp_types.h"
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <kj/debug.h>
#include <kj/string.h>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

bool contains(kj::StringPtr haystack, kj::StringPtr needle) {
  if (needle.size() > haystack.size()) {
    return false;
  }

  for (size_t i = 0; i <= haystack.size() - needle.size(); i++) {
    if (haystack.slice(i, i + needle.size()) == needle) {
      return true;
    }
  }

  return false;
}

} // namespace

int main() {
  // Test case (a): response with a string result
  {
    capnp::MallocMessageBuilder resultBuilder;
    auto resultRoot = resultBuilder.initRoot<capnp::JsonValue>();
    auto resultObj = resultRoot.initObject(1);
    resultObj[0].setName("result");
    resultObj[0].getValue().setString("test-string");

    auto response = capnp_ls::buildResponseString(1.0, resultBuilder.getRoot<capnp::JsonValue>());
    require(
        response != CAPNP_LS_NONE,
        "buildResponseString should return a string for valid response");

    CAPNP_LS_IF_SOME(responseStr, response) {
      // Verify it contains Content-Length header
      require(
          responseStr->startsWith("Content-Length: "),
          "response should start with Content-Length header");

      // Verify it contains the delimiter
      require(
          contains(*responseStr, "\r\n\r\n"),
          "response should contain CRLF delimiter");

      // Verify it contains the string result
      require(
          contains(*responseStr, "\"result\":\"test-string\""),
          "response should contain the string result");

      KJ_LOG(INFO, "Test (a) passed: string result response");
    }
  }

  // Test case (b): response with empty object result -> result serializes as null
  {
    capnp::MallocMessageBuilder resultBuilder;
    auto resultRoot = resultBuilder.initRoot<capnp::JsonValue>();
    auto resultObj = resultRoot.initObject(0); // empty object

    auto response = capnp_ls::buildResponseString(2.0, resultBuilder.getRoot<capnp::JsonValue>());
    require(
        response != CAPNP_LS_NONE,
        "buildResponseString should return a string for empty object");

    CAPNP_LS_IF_SOME(responseStr, response) {
      // Verify it contains Content-Length header
      require(
          responseStr->startsWith("Content-Length: "),
          "response should start with Content-Length header");

      // Verify it contains the delimiter
      require(
          contains(*responseStr, "\r\n\r\n"),
          "response should contain CRLF delimiter");

      // Verify result is null (not an empty object)
      require(
          contains(*responseStr, "\"result\":null"),
          "response with empty object result should serialize as null");

      KJ_LOG(INFO, "Test (b) passed: empty object result -> null");
    }
  }

  // Test case (c): error response with code/message and correct Content-Length framing
  {
    auto errorResponse = capnp_ls::buildErrorResponseString(3.0, -32600, "Invalid Request");
    require(
        errorResponse != CAPNP_LS_NONE,
        "buildErrorResponseString should return a string");

    CAPNP_LS_IF_SOME(errorStr, errorResponse) {
      // Verify it starts with Content-Length header
      require(
          errorStr->startsWith("Content-Length: "),
          "error response should start with Content-Length header");

      // Verify it contains the delimiter
      require(
          contains(*errorStr, "\r\n\r\n"),
          "error response should contain CRLF delimiter");

      // Verify it contains error code
      require(
          contains(*errorStr, "\"code\":-32600"),
          "error response should contain error code");

      // Verify it contains error message
      require(
          contains(*errorStr, "\"message\":\"Invalid Request\""),
          "error response should contain error message");

      KJ_LOG(INFO, "Test (c) passed: error response with correct framing");
    }
  }

  KJ_LOG(INFO, "All json_rpc tests passed");
  return 0;
}
