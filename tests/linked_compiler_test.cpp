// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "linked_compiler.h"
#include "symbol_resolver.h"
#include <capnp/schema.capnp.h>
#include <kj/debug.h>
#include <kj/string.h>

namespace {

void require(bool condition, kj::StringPtr message) {
  if (!condition) {
    KJ_FAIL_REQUIRE(message);
  }
}

kj::String fixturePath(kj::StringPtr path) {
  return kj::str(CAPNP_LS_TEST_FIXTURE_DIR, "/", path);
}

uint64_t resolveAt(
    const kj::HashMap<capnp_ls::Range, uint64_t> &rangeMap,
    uint32_t line,
    uint32_t character) {
  kj::Maybe<uint64_t> resolvedId = nullptr;
  for (const auto &[range, id] : rangeMap) {
    if (range.start.line <= line && line <= range.end.line &&
        range.start.character <= character && character <= range.end.character) {
      resolvedId = id;
      break;
    }
  }

  return KJ_ASSERT_NONNULL(resolvedId);
}

void requireDiagnosticContaining(
    const kj::HashMap<kj::String, kj::Vector<capnp_ls::Diagnostic>> &diagnostics,
    kj::StringPtr message) {
  for (const auto &[filePath, fileDiagnostics] : diagnostics) {
    for (const auto &diagnostic : fileDiagnostics) {
      if (diagnostic.message.startsWith(message)) {
        return;
      }
    }
  }

  KJ_FAIL_REQUIRE("expected diagnostic was not found", message);
}

} // namespace

int main() {
  kj::Vector<kj::String> importPaths;
  importPaths.add(kj::heapString("schemas/common"));

  kj::HashMap<kj::String, kj::Vector<capnp_ls::Diagnostic>> diagnostics;

  auto validResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      importPaths,
      diagnostics);

  require(validResult.success, "valid fixture should compile");
  require(diagnostics.size() == 0, "valid fixture should not produce diagnostics");

  auto &requestMessage = KJ_ASSERT_NONNULL(validResult.request);
  auto request =
      requestMessage->getRoot<capnp::schema::CodeGeneratorRequest>();
  require(request.getNodes().size() > 0, "request should contain nodes");
  require(
      request.getRequestedFiles().size() == 1,
      "request should contain one requested file");
  require(
      request.getRequestedFiles()[0]
              .getFileSourceInfo()
              .getIdentifiers()
              .size() > 0,
      "request should contain identifier resolutions");

  kj::HashMap<kj::String, kj::HashMap<capnp_ls::Range, uint64_t>>
      positionToNodeIdMap;
  kj::HashMap<uint64_t, kj::Own<capnp_ls::Location>> nodeLocationMap;
  auto resolveResult = capnp_ls::SymbolResolver::resolve(
      request, positionToNodeIdMap, nodeLocationMap, importPaths,
      CAPNP_LS_TEST_FIXTURE_DIR);
  require(resolveResult == 0, "symbol resolver should resolve valid fixture");

  auto companyPath = fixturePath("schemas/company.capnp");
  auto commonPath = fixturePath("schemas/common/common.capnp");
  auto &rangeMap = KJ_ASSERT_NONNULL(positionToNodeIdMap.find(companyPath));

  auto employeeId = resolveAt(rangeMap, 15, 29);
  auto &location = KJ_ASSERT_NONNULL(nodeLocationMap.find(employeeId));
  require(location->uri == companyPath, "definition should resolve in company.capnp");
  require(location->range.start.line == 18, "definition should start at line 18");
  require(location->range.start.character == 3, "definition should start at character 3");

  auto qualifiedImportId = resolveAt(rangeMap, 21, 22);
  auto &qualifiedImportLocation =
      KJ_ASSERT_NONNULL(nodeLocationMap.find(qualifiedImportId));
  require(
      qualifiedImportLocation->uri == commonPath,
      "qualified imported definition should resolve in common.capnp");
  require(
      qualifiedImportLocation->range.start.line == 8,
      "qualified imported definition should start at enum line");

  auto inlineImportId = resolveAt(rangeMap, 22, 40);
  auto &inlineImportLocation =
      KJ_ASSERT_NONNULL(nodeLocationMap.find(inlineImportId));
  require(
      inlineImportLocation->uri == commonPath,
      "inline imported definition should resolve in common.capnp");
  require(
      inlineImportLocation->range.start.line == 8,
      "inline imported definition should start at enum line");

  kj::Vector<kj::String> noImportPaths;
  diagnostics.clear();
  auto standardImportResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/standard_import.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      noImportPaths,
      diagnostics);
  require(
      standardImportResult.success,
      "standard Cap'n Proto imports should compile without user import paths");
  require(
      diagnostics.size() == 0,
      "standard Cap'n Proto imports should not produce diagnostics");

  auto &standardImportRequestMessage =
      KJ_ASSERT_NONNULL(standardImportResult.request);
  auto standardImportRequest =
      standardImportRequestMessage->getRoot<capnp::schema::CodeGeneratorRequest>();
  positionToNodeIdMap.clear();
  nodeLocationMap.clear();
  resolveResult = capnp_ls::SymbolResolver::resolve(
      standardImportRequest, positionToNodeIdMap, nodeLocationMap, noImportPaths,
      CAPNP_LS_TEST_FIXTURE_DIR);
  require(
      resolveResult == 0,
      "symbol resolver should resolve standard Cap'n Proto imports");

  kj::Vector<kj::String> badImportPaths;
  badImportPaths.add(kj::heapString("schemas/common"));
  badImportPaths.add(kj::heapString("schemas/does-not-exist"));
  diagnostics.clear();
  auto badImportPathResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      badImportPaths,
      diagnostics);
  require(!badImportPathResult.success, "missing import path should fail");
  requireDiagnosticContaining(diagnostics, "Import path does not exist");

  diagnostics.clear();
  auto invalidResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/error_company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      importPaths,
      diagnostics);

  require(!invalidResult.success, "invalid fixture should fail");
  require(diagnostics.size() > 0, "invalid fixture should produce diagnostics");

  return 0;
}
