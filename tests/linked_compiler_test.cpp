// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "linked_compiler.h"
#include "kj_compat.h"
#include "log_level.h"
#include "project_config.h"
#include "symbol_index.h"
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
  kj::Maybe<uint64_t> resolvedId = CAPNP_LS_NONE;
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

bool hasDocumentSymbol(
    const kj::Vector<capnp_ls::DocumentSymbol> &symbols,
    kj::StringPtr name,
    uint32_t kind) {
  for (const auto &symbol : symbols) {
    if (symbol.name == name && symbol.kind == kind) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  kj::Vector<kj::String> importPaths;
  importPaths.add(kj::heapString("schemas/common"));

  kj::Vector<kj::String> configuredImportPaths;
  require(
      capnp_ls::loadProjectConfigImportPaths(
          CAPNP_LS_TEST_FIXTURE_DIR,
          configuredImportPaths),
      "project config should load from fixture");
  require(
      configuredImportPaths.size() == 2,
      "project config should include two import paths");
  require(
      configuredImportPaths[0] == "schemas/common",
      "project config should preserve import path order");

  capnp_ls::ProjectConfig parsedConfig;
  capnp_ls::parseProjectConfig(
      R"({"importPaths":["schemas/common"],"logLevel":"info"})",
      parsedConfig);
  require(
      parsedConfig.importPaths.size() == 1,
      "project config should parse import paths");
  require(
      KJ_ASSERT_NONNULL(parsedConfig.logLevel) == capnp_ls::LogLevel::INFO,
      "project config should parse log level");

  bool invalidLogLevelFailed = false;
  try {
    capnp_ls::ProjectConfig invalidConfig;
    capnp_ls::parseProjectConfig(
        R"({"importPaths":["schemas/common"],"logLevel":"debug"})",
        invalidConfig);
  } catch (kj::Exception &) {
    invalidLogLevelFailed = true;
  }
  require(
      invalidLogLevelFailed,
      "project config should reject unknown log levels");

  capnp_ls::SymbolIndex index;

  auto configuredResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      configuredImportPaths,
      index.diagnosticMap);
  require(configuredResult.success, "project config import paths should compile");
  require(
      index.diagnosticMap.size() == 0,
      "project config import paths should not produce diagnostics");
  index.diagnosticMap.clear();

  auto validResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      importPaths,
      index.diagnosticMap);

  require(validResult.success, "valid fixture should compile");
  require(index.diagnosticMap.size() == 0, "valid fixture should not produce diagnostics");

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

  auto resolveResult = capnp_ls::SymbolResolver::resolve(
      request, index, importPaths, CAPNP_LS_TEST_FIXTURE_DIR);
  require(resolveResult == 0, "symbol resolver should resolve valid fixture");

  auto companyPath = fixturePath("schemas/company.capnp");
  auto commonPath = fixturePath("schemas/common/common.capnp");
  auto &rangeMap = KJ_ASSERT_NONNULL(index.fileSourceInfoMap.find(companyPath));

  auto employeeId = resolveAt(rangeMap, 15, 29);
  auto &location = KJ_ASSERT_NONNULL(index.nodeLocationMap.find(employeeId));
  require(location->uri == companyPath, "definition should resolve in company.capnp");
  require(location->range.start.line == 18, "definition should start at line 18");
  require(location->range.start.character == 3, "definition should start at character 3");
  auto &employeeMetadata = KJ_ASSERT_NONNULL(index.nodeMetadataMap.find(employeeId));
  require(employeeMetadata->name == "Employee", "hover metadata should use short symbol name");
  require(employeeMetadata->detail == "struct", "hover metadata should include symbol kind");
  auto &employeeReferences = KJ_ASSERT_NONNULL(index.referenceMap.find(employeeId));
  require(
      employeeReferences.size() >= 4,
      "references should include declaration and type usages");
  auto &companySymbols = KJ_ASSERT_NONNULL(index.documentSymbolMap.find(companyPath));
  require(
      hasDocumentSymbol(companySymbols, "EmployeeManagement", 11),
      "document symbols should include interfaces");
  require(
      hasDocumentSymbol(companySymbols, "addEmployee", 6),
      "document symbols should include methods");
  require(
      hasDocumentSymbol(companySymbols, "Employee", 23),
      "document symbols should include nested structs");

  auto qualifiedImportId = resolveAt(rangeMap, 21, 22);
  auto &qualifiedImportLocation =
      KJ_ASSERT_NONNULL(index.nodeLocationMap.find(qualifiedImportId));
  require(
      qualifiedImportLocation->uri == commonPath,
      "qualified imported definition should resolve in common.capnp");
  require(
      qualifiedImportLocation->range.start.line == 8,
      "qualified imported definition should start at enum line");

  auto inlineImportId = resolveAt(rangeMap, 22, 40);
  auto &inlineImportLocation =
      KJ_ASSERT_NONNULL(index.nodeLocationMap.find(inlineImportId));
  require(
      inlineImportLocation->uri == commonPath,
      "inline imported definition should resolve in common.capnp");
  require(
      inlineImportLocation->range.start.line == 8,
      "inline imported definition should start at enum line");

  // Resolving the same request again simulates a recompile; it must not
  // duplicate document symbols or references, including for imported files.
  auto companySymbolCount = companySymbols.size();
  auto commonSymbolCount =
      KJ_ASSERT_NONNULL(index.documentSymbolMap.find(commonPath)).size();
  auto employeeReferenceCount = employeeReferences.size();
  auto qualifiedImportReferenceCount =
      KJ_ASSERT_NONNULL(index.referenceMap.find(qualifiedImportId)).size();
  resolveResult = capnp_ls::SymbolResolver::resolve(
      request, index, importPaths, CAPNP_LS_TEST_FIXTURE_DIR);
  require(resolveResult == 0, "symbol resolver should resolve fixture again");
  require(
      KJ_ASSERT_NONNULL(index.documentSymbolMap.find(companyPath)).size() ==
          companySymbolCount,
      "recompile should not duplicate requested file document symbols");
  require(
      KJ_ASSERT_NONNULL(index.documentSymbolMap.find(commonPath)).size() ==
          commonSymbolCount,
      "recompile should not duplicate imported file document symbols");
  require(
      KJ_ASSERT_NONNULL(index.referenceMap.find(employeeId)).size() ==
          employeeReferenceCount,
      "recompile should not duplicate requested file references");
  require(
      KJ_ASSERT_NONNULL(index.referenceMap.find(qualifiedImportId)).size() ==
          qualifiedImportReferenceCount,
      "recompile should not duplicate imported file references");

  kj::Vector<kj::String> noImportPaths;
  index.diagnosticMap.clear();
  auto standardImportResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/standard_import.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      noImportPaths,
      index.diagnosticMap);
  require(
      standardImportResult.success,
      "standard Cap'n Proto imports should compile without user import paths");
  require(
      index.diagnosticMap.size() == 0,
      "standard Cap'n Proto imports should not produce diagnostics");

  auto &standardImportRequestMessage =
      KJ_ASSERT_NONNULL(standardImportResult.request);
  auto standardImportRequest =
      standardImportRequestMessage->getRoot<capnp::schema::CodeGeneratorRequest>();
  index.fileSourceInfoMap.clear();
  index.nodeLocationMap.clear();
  index.nodeMetadataMap.clear();
  index.referenceMap.clear();
  index.documentSymbolMap.clear();
  resolveResult = capnp_ls::SymbolResolver::resolve(
      standardImportRequest, index, noImportPaths,
      CAPNP_LS_TEST_FIXTURE_DIR);
  require(
      resolveResult == 0,
      "symbol resolver should resolve standard Cap'n Proto imports");

  kj::Vector<kj::String> badImportPaths;
  badImportPaths.add(kj::heapString("schemas/common"));
  badImportPaths.add(kj::heapString("schemas/does-not-exist"));
  index.diagnosticMap.clear();
  auto badImportPathResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      badImportPaths,
      index.diagnosticMap);
  require(!badImportPathResult.success, "missing import path should fail");
  requireDiagnosticContaining(index.diagnosticMap, "Import path does not exist");

  index.diagnosticMap.clear();
  auto invalidResult = capnp_ls::LinkedCompiler::compile(
      fixturePath("schemas/error_company.capnp"),
      CAPNP_LS_TEST_FIXTURE_DIR,
      importPaths,
      index.diagnosticMap);

  require(!invalidResult.success, "invalid fixture should fail");
  require(index.diagnosticMap.size() > 0, "invalid fixture should produce diagnostics");

  return 0;
}
