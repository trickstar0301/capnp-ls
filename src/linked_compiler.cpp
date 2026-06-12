// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "linked_compiler.h"
#include "kj_compat.h"
#include <capnp/compiler/compiler.h>
#include <capnp/compiler/module-loader.h>
#include <capnp/schema.capnp.h>
#include <kj/debug.h>
#include <kj/filesystem.h>

namespace capnp_ls {
namespace {

class DiagnosticReporter final : public capnp::compiler::GlobalErrorReporter {
public:
  explicit DiagnosticReporter(
      kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap)
      : diagnosticMap(diagnosticMap) {}

  void addDirectoryPrefix(const kj::ReadableDirectory &directory,
                          kj::StringPtr prefix) {
    directoryPrefixes.upsert(&directory, kj::heapString(prefix));
  }

  void addError(const kj::ReadableDirectory &directory, kj::PathPtr path,
                SourcePos start, SourcePos end,
                kj::StringPtr message) override {
    hadErrors_ = true;

    kj::String filePath;
    CAPNP_LS_IF_SOME (prefix, directoryPrefixes.find(&directory)) {
      filePath = kj::str(*prefix, path.toString());
    } else {
      filePath = path.toString();
    }

    Diagnostic diagnostic;
    diagnostic.range.start = {
        static_cast<uint32_t>(start.line),
        static_cast<uint32_t>(start.column),
    };
    diagnostic.range.end = {
        static_cast<uint32_t>(end.line),
        static_cast<uint32_t>(end.column),
    };
    diagnostic.severity = DiagnosticSeverity::Error;
    diagnostic.message = kj::heapString(message);
    diagnostic.source = kj::heapString("capnp-compiler");

    auto &diagnostics = diagnosticMap.findOrCreate(
        filePath, [&]() -> kj::HashMap<kj::String, kj::Vector<Diagnostic>>::
                    Entry {
                      return {kj::mv(filePath), kj::Vector<Diagnostic>()};
                    });
    diagnostics.add(kj::mv(diagnostic));
  }

  bool hadErrors() override { return hadErrors_; }

private:
  kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap;
  kj::HashMap<const kj::ReadableDirectory *, kj::String> directoryPrefixes;
  bool hadErrors_ = false;
};

struct SourceDirectory {
  kj::Own<const kj::ReadableDirectory> dir;
  kj::String prefix;
};

kj::String withTrailingSlash(kj::StringPtr path) {
  if (path.size() == 0 || path.endsWith("/")) {
    return kj::heapString(path);
  }
  return kj::str(path, "/");
}

kj::Path parseAbsolutePath(kj::StringPtr path) {
  if (path.startsWith("/")) {
    return kj::Path::parse(path.slice(1));
  }
  return kj::Path::parse(path);
}

kj::Maybe<kj::String> relativeTo(kj::StringPtr fileName, kj::StringPtr baseDir) {
  if (fileName == baseDir) {
    return kj::heapString("");
  }

  auto prefix = withTrailingSlash(baseDir);
  if (fileName.startsWith(prefix)) {
    return kj::heapString(fileName.slice(prefix.size()));
  }
  return CAPNP_LS_NONE;
}

kj::String resolveDirectoryPath(kj::StringPtr path, kj::StringPtr workingDir) {
  if (path.startsWith("/")) {
    return kj::heapString(path);
  }
  return kj::str(withTrailingSlash(workingDir), path);
}

void addCompilerDiagnostic(
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap,
    kj::StringPtr filePath,
    kj::StringPtr message) {
  Diagnostic diagnostic;
  diagnostic.range.start = {0, 0};
  diagnostic.range.end = {0, 0};
  diagnostic.severity = DiagnosticSeverity::Error;
  diagnostic.message = kj::heapString(message);
  diagnostic.source = kj::heapString("capnp-compiler");

  auto &diagnostics = diagnosticMap.findOrCreate(
      kj::str(filePath),
      [&]() -> kj::HashMap<kj::String, kj::Vector<Diagnostic>>::Entry {
        return {kj::str(filePath), kj::Vector<Diagnostic>()};
      });
  diagnostics.add(kj::mv(diagnostic));
}

kj::Maybe<const kj::ReadableDirectory &> tryOpenDirectory(
    kj::Filesystem &filesystem,
    kj::StringPtr path,
    kj::Vector<SourceDirectory> &ownedDirectories,
    DiagnosticReporter &reporter) {
  if (path.size() == 0 || path == "/") {
    auto &root = filesystem.getRoot();
    reporter.addDirectoryPrefix(root, "/");
    return root;
  }

  auto parsed = parseAbsolutePath(path);
  CAPNP_LS_IF_SOME (dir, filesystem.getRoot().tryOpenSubdir(parsed)) {
    auto &result = **dir;
    reporter.addDirectoryPrefix(result, withTrailingSlash(path));
    ownedDirectories.add(SourceDirectory{kj::mv(*dir), withTrailingSlash(path)});
    return result;
  }

  return CAPNP_LS_NONE;
}

bool addRequiredImportPath(
    capnp::compiler::ModuleLoader &loader,
    kj::Filesystem &filesystem,
    kj::StringPtr path,
    kj::Vector<SourceDirectory> &ownedDirectories,
    DiagnosticReporter &reporter,
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap,
    kj::StringPtr diagnosticFile) {
  CAPNP_LS_IF_SOME (dir, tryOpenDirectory(
                       filesystem, path, ownedDirectories, reporter)) {
    loader.addImportPath(*dir);
    return true;
  }

  addCompilerDiagnostic(
      diagnosticMap,
      diagnosticFile,
      kj::str("Import path does not exist: ", path));
  return false;
}

void addOptionalImportPath(
    capnp::compiler::ModuleLoader &loader,
    kj::Filesystem &filesystem,
    kj::StringPtr path,
    kj::Vector<SourceDirectory> &ownedDirectories,
    DiagnosticReporter &reporter) {
  CAPNP_LS_IF_SOME (dir, tryOpenDirectory(
                       filesystem, path, ownedDirectories, reporter)) {
    loader.addImportPath(*dir);
  }
}

struct SourceFile {
  uint64_t id;
  capnp::compiler::Compiler::ModuleScope compiled;
  kj::StringPtr name;
  capnp::compiler::Module *module;
};

void populateFileSourceInfo(
    capnp::schema::CodeGeneratorRequest::RequestedFile::Builder requestedFile,
    capnp::compiler::Module &module) {
  auto fileSourceInfo = requestedFile.initFileSourceInfo();
  auto resolutions = module.getResolutions();
  auto identifiers = fileSourceInfo.initIdentifiers(resolutions.size());

  for (size_t i = 0; i < resolutions.size(); ++i) {
    auto identifier = identifiers[i];
    identifier.setStartByte(resolutions[i].startByte);
    identifier.setEndByte(resolutions[i].endByte);
    KJ_SWITCH_ONEOF(resolutions[i].target) {
      KJ_CASE_ONEOF(type, capnp::compiler::Resolution::Type) {
        identifier.setTypeId(type.typeId);
      }
      KJ_CASE_ONEOF(member, capnp::compiler::Resolution::Member) {
        auto memberBuilder = identifier.initMember();
        memberBuilder.setParentTypeId(member.parentTypeId);
        memberBuilder.setOrdinal(member.ordinal);
      }
    }
  }
}

} // namespace

LinkedCompileResult LinkedCompiler::compile(
    kj::StringPtr fileName,
    kj::StringPtr workingDir,
    const kj::Vector<kj::String> &importPaths,
    kj::Filesystem &filesystem,
    kj::HashMap<kj::String, kj::Vector<Diagnostic>> &diagnosticMap) {
  diagnosticMap.clear();

  DiagnosticReporter reporter(diagnosticMap);
  kj::Vector<SourceDirectory> ownedDirectories;

  const kj::ReadableDirectory *workspaceDir = nullptr;
  CAPNP_LS_IF_SOME (dir, tryOpenDirectory(
                       filesystem, workingDir, ownedDirectories, reporter)) {
    workspaceDir = dir;
  } else {
    addCompilerDiagnostic(
        diagnosticMap,
        fileName,
        kj::str("Workspace directory does not exist: ", workingDir));
    return LinkedCompileResult{};
  }

  capnp::compiler::ModuleLoader loader(reporter);
  for (const auto &importPath : importPaths) {
    auto resolvedImportPath = resolveDirectoryPath(importPath, workingDir);
    if (!addRequiredImportPath(
            loader,
            filesystem,
            resolvedImportPath,
            ownedDirectories,
            reporter,
            diagnosticMap,
            fileName)) {
      return LinkedCompileResult{};
    }
  }

  addOptionalImportPath(
      loader,
      filesystem,
      kj::str(CAPNP_LS_CAPNP_SOURCE_DIR, "/src"),
      ownedDirectories,
      reporter);

  auto relativeFileName = relativeTo(fileName, workingDir);
  kj::Path sourcePath = parseAbsolutePath(fileName);
  const kj::ReadableDirectory *sourceDir = &filesystem.getRoot();
  CAPNP_LS_IF_SOME (relative, relativeFileName) {
    sourcePath = kj::Path::parse(*relative);
    sourceDir = workspaceDir;
  }

  capnp::compiler::Compiler compiler;
  kj::Vector<SourceFile> sourceFiles;

  CAPNP_LS_IF_SOME (module, loader.loadModule(*sourceDir, sourcePath)) {
    auto compiled = compiler.add(*module);
    compiler.eagerlyCompile(
        compiled.getId(),
        capnp::compiler::Compiler::NODE |
            capnp::compiler::Compiler::CHILDREN |
            capnp::compiler::Compiler::DEPENDENCIES |
            capnp::compiler::Compiler::DEPENDENCY_PARENTS);
    sourceFiles.add(SourceFile{
        compiled.getId(),
        compiled,
        module->getSourceName(),
        module,
    });
  } else {
    addCompilerDiagnostic(
        diagnosticMap,
        fileName,
        kj::str("No such file: ", fileName));
    return LinkedCompileResult{};
  }

  if (reporter.hadErrors()) {
    return LinkedCompileResult{};
  }

  auto message = kj::heap<capnp::MallocMessageBuilder>();
  auto request = message->initRoot<capnp::schema::CodeGeneratorRequest>();

  auto version = request.getCapnpVersion();
  version.setMajor(CAPNP_VERSION_MAJOR);
  version.setMinor(CAPNP_VERSION_MINOR);
  version.setMicro(CAPNP_VERSION_MICRO);

  auto schemas = compiler.getLoader().getAllLoaded();
  auto nodes = request.initNodes(schemas.size());
  for (size_t i = 0; i < schemas.size(); ++i) {
    nodes.setWithCaveats(i, schemas[i].getProto());
  }

  request.adoptSourceInfo(compiler.getAllSourceInfo(message->getOrphanage()));

  auto requestedFiles = request.initRequestedFiles(sourceFiles.size());
  for (size_t i = 0; i < sourceFiles.size(); ++i) {
    auto requestedFile = requestedFiles[i];
    requestedFile.setId(sourceFiles[i].id);
    requestedFile.setFilename(sourceFiles[i].name);
    requestedFile.adoptImports(compiler.getFileImportTable(
        *sourceFiles[i].module,
        capnp::Orphanage::getForMessageContaining(requestedFile)));
    populateFileSourceInfo(requestedFile, *sourceFiles[i].module);
  }

  LinkedCompileResult result;
  result.success = true;
  result.request = kj::mv(message);
  return result;
}

} // namespace capnp_ls
