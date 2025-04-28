// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "compilation_manager.h"
#include "compile_error_parser.h"
#include "utils.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string.h>
#include <regex>

namespace capnp_ls {

CompilationManager::CompilationManager(kj::AsyncIoContext &ioContext)
    : subprocessRunner(ioContext) {}

kj::Promise<void> CompilationManager::compile(CompileParams params) {
  return checkCapnpVersionCompatible(params.compilerPath)
      .then([this, params](bool isCompatible) {
        if (!isCompatible) {
          KJ_LOG(FATAL, "Cap'n Proto version is not compatible");
          return kj::Promise<void>(kj::READY_NOW);
        } else {
          KJ_LOG(INFO, "Compiling:", params.fileName);
          kj::String strippedUri = kj::heapString(params.fileName);
          if (strippedUri.startsWith(params.workingDir)) {
            strippedUri =
                kj::heapString(strippedUri.slice(params.workingDir.size() + 1));
          }
          params.diagnosticMap.clear();
          KJ_IF_MAYBE (command, buildCommand(params)) {
            return subprocessRunner
                .run(
                    {.command = *command,
                     .workingDir = params.workingDir,
                     .isCapnpMessageOutput = true})
                .then([params, fileName = kj::mv(strippedUri)](
                          SubprocessRunner::RunResult result) mutable {
                  if (result.exitCode != 0) {
                    KJ_LOG(
                        ERROR, "Failed to compile", fileName, result.errorText);
                    int status = CompileErrorParser::parse(
                        fileName, result.errorText, params.diagnosticMap);
                    if (status != 0) {
                      KJ_LOG(
                          ERROR,
                          "Failed to parse compile errors",
                          fileName,
                          result.errorText);
                    }
                    return kj::Promise<void>(kj::READY_NOW);
                  }

                  KJ_IF_MAYBE (reader, result.maybeReader) {
                    SymbolResolver::resolve(
                        kj::mv(*reader),
                        params.fileSourceInfoMap,
                        params.nodeLocationMap,
                        params.importPaths,
                        params.workingDir);
                  }
                  return kj::Promise<void>(kj::READY_NOW);
                })
                .catch_([](kj::Exception &&e) {
                  KJ_LOG(
                      ERROR,
                      "Compilation error, exception:",
                      e.getDescription());
                  return kj::Promise<void>(kj::READY_NOW);
                });
          }
          return kj::Promise<void>(kj::READY_NOW);
        }
      })
      .catch_([](kj::Exception &&e) {
        KJ_LOG(ERROR, "Version check error, exception:", e.getDescription());
        return kj::Promise<void>(kj::READY_NOW);
      });
}

kj::Promise<bool>
CompilationManager::checkCapnpVersionCompatible(kj::StringPtr compilerPath) {
  if (isCapnpVersionCompatible) {
    return kj::Promise<bool>(true);
  }
  if (compilerPath == nullptr) {
    KJ_LOG(FATAL, "Compiler path is not specified");
    return kj::Promise<bool>(false);
  }

  auto command = kj::str(compilerPath, " --version");
  KJ_LOG(INFO, "Checking capnp version with command:", command);
  SubprocessRunner::RunParams params = {
      .command = command, .workingDir = ".", .isCapnpMessageOutput = false};
  return subprocessRunner.run(params)
      .then([this](SubprocessRunner::RunResult result) -> bool {
        if (result.status != SubprocessRunner::Status::SUCCESS) {
          KJ_LOG(ERROR, "Failed to check capnp version:", result.errorText);
          return false;
        }

        if (result.textOutput == nullptr) {
          KJ_LOG(ERROR, "No version output received");
          if (result.errorText.size() > 0) {
            KJ_LOG(ERROR, "Error text:", result.errorText);
          }
          return false;
        }

        try {
          // Parse version string from errorText (stderr)
          std::string outputStr(result.textOutput.cStr());
          KJ_LOG(INFO, "Version output:", outputStr);

          std::regex versionRegex(R"(Cap'n Proto version (\d+)\.(\d+))");
          std::smatch matches;

          if (!std::regex_search(outputStr, matches, versionRegex)) {
            KJ_LOG(ERROR, "Version string format mismatch:", outputStr);
            return false;
          }

          if (matches.size() != 3) {
            KJ_LOG(
                ERROR,
                "Unexpected number of version components:",
                matches.size());
            return false;
          }

          try {
            int major = std::stoi(matches[1].str());
            int minor = std::stoi(matches[2].str());

            KJ_LOG(INFO, "Parsed version:", major, ".", minor);

            // Check if version is at least 1.1.0
            if (major > 1) {
              isCapnpVersionCompatible = true;
              return true;
            }
            if (major == 1 && minor >= 1) {
              isCapnpVersionCompatible = true;
              return true;
            }

            KJ_LOG(ERROR, "Unsupported version:", major, ".", minor);
            return false;
          } catch (const std::invalid_argument &e) {
            KJ_LOG(ERROR, "Invalid version number format:", e.what());
            return false;
          } catch (const std::out_of_range &e) {
            KJ_LOG(ERROR, "Version number out of range:", e.what());
            return false;
          }
        } catch (const std::regex_error &e) {
          KJ_LOG(ERROR, "Regex error:", e.what());
          return false;
        } catch (const kj::Exception &e) {
          KJ_LOG(ERROR, "Error parsing version output:", e.getDescription());
          return false;
        } catch (const std::exception &e) {
          KJ_LOG(ERROR, "Unexpected error:", e.what());
          return false;
        }
      })
      .catch_([](kj::Exception &&e) {
        KJ_LOG(ERROR, "Version check error:", e.getDescription());
        return false;
      });
}

kj::Maybe<kj::String> CompilationManager::buildCommand(CompileParams params) {
  kj::Vector<kj::String> args;
  kj::String compilerPath;
  if (params.compilerPath != nullptr && params.compilerPath.size() > 0) {
    compilerPath = kj::heapString(params.compilerPath);
    KJ_LOG(INFO, "Using user-specified capnp compiler", compilerPath);
  } else {
#ifdef BUNDLED_CAPNP_EXECUTABLE
    compilerPath = kj::heapString(BUNDLED_CAPNP_EXECUTABLE);
    KJ_LOG(INFO, "Using bundled capnp compiler", compilerPath);
#else
    compilerPath = kj::heapString("capnp");
    KJ_LOG(INFO, "Using default capnp command", compilerPath);
#endif
  }

  if (!compilerPath.endsWith("capnp")) {
    KJ_LOG(ERROR, "Compiler path must end with 'capnp'");
    return nullptr;
  }
  args.add(kj::mv(compilerPath));
  args.add(kj::heapString("compile"));

  for (auto &path : params.importPaths) {
    args.add(kj::str("-I", path));
  }

  args.add(kj::str("-o", "-")); // output to stdout
  args.add(kj::heapString(params.fileName));

  kj::String result;
  for (auto &arg : args) {
    if (result.size() > 0)
      result = kj::str(result, " ");
    if (arg.findFirst(' ') != nullptr || arg.findFirst('\t') != nullptr) {
      result = kj::str(result, "\"", arg, "\"");
    } else {
      result = kj::str(result, arg);
    }
  }

  return result;
}
} // namespace capnp_ls