# capnp-ls Refactoring Plan

Planner/reviewer: Fable 5. Executor: lighter model (e.g. Haiku), one task per session.
Goal: **zero behavior change**. Every task must leave the tree green.

## Why

`src/lsp_message_handler.cpp` (~1000 lines) is a god class mixing five concerns:
JSON-RPC framing, JSON (de)serialization, LSP feature logic, workspace/config
state, and the symbol index (6 hash maps threaded by reference through
`CompilationManager::CompileParams` and `SymbolResolver::resolve`, which takes
7 parameters). Pure logic is locked inside private methods, so only 2
integration-style tests exist. We extract pure modules first (testable, low
risk), then group the index state, then slim the handler.

## Global rules for the executor (read before every task)

1. **Verify** after every task: `just test` (uses the preconfigured `build/`
   dir). All targets must compile and all ctest tests must pass. Do not
   proceed if red; revert and report instead of improvising.
   Known caveat: the final `tests/install_sh_test.sh` step may fail with
   `mktemp ... Operation not permitted` when run in a sandboxed shell — that
   is a sandbox artifact, unrelated to these tasks. The gate is
   `cmake --build` + `ctest` passing (baseline verified green on 2026-06-12).
2. **KJ idioms only** — this codebase does not use the STL in src/ (tests may):
   `kj::String`/`kj::StringPtr`, `kj::Vector`, `kj::HashMap`, `kj::Maybe`,
   `kj::mv`, `kj::str`, `kj::heapString`. For `kj::Maybe` access use the
   `CAPNP_LS_IF_SOME` / `CAPNP_LS_NONE` macros from `src/kj_compat.h`
   (they paper over capnp v1/v2 differences). Never use `KJ_IF_SOME` directly.
3. **C++17 compatible** — the project is built against capnp v1.1–v2 with
   C++17 and C++23. No C++20+ features.
4. **Move, don't rewrite.** When a task says "move function X", cut and paste
   the body verbatim; change only what the task lists (namespace, includes,
   signature). Do not "improve" logic, rename variables, reorder fields, or
   reformat untouched code.
5. **New files** get the same 4-line MIT license header as existing files,
   `#pragma once` in headers, and live in namespace `capnp_ls`.
6. **CMake**: when adding a `.cpp`, add it to ALL three targets that need it in
   `CMakeLists.txt` (`capnp-ls`, `capnp-ls-initialize-config-test`, and
   `capnp-ls-linked-compiler-test` if it's in that test's dependency set).
7. **TDD where the task adds a test**: write the test first, see it fail to
   compile/link (Red), then move the code (Green).
8. Commit per task, message format: `refactor: <task id> <short title>`
   (English). Do not push.

## Review gates

After each phase the planner (Fable) reviews `git diff` of the phase before the
next phase starts. Executor stops at gate markers.

---

## Phase 1 — extract pure helpers from lsp_message_handler.cpp

### T1: Range geometry → lsp_types

- Move `containsPosition(const Range&, uint32_t line, uint32_t character)`
  (lsp_message_handler.cpp:96) and `isTighterRange(const Range&, const Range&)`
  (lsp_message_handler.cpp:109) out of the anonymous namespace of
  `lsp_message_handler.cpp` into `lsp_types.h` (declarations) +
  `lsp_types.cpp` (definitions), as free functions in `capnp_ls`.
- New test `tests/lsp_types_test.cpp` (own `main()`, use the `require()`
  pattern from `tests/linked_compiler_test.cpp`): cover
  - point inside / before-start / after-end of a multi-line range;
  - same-line boundary characters (start char, end char are inclusive);
  - `isTighterRange`: fewer lines wins; equal lines → smaller char span wins.
- CMake: new target `capnp-ls-lsp-types-test` linking `tests/lsp_types_test.cpp`
  + `src/lsp_types.cpp` against `kj` only, plus
  `add_test(NAME lsp-types COMMAND capnp-ls-lsp-types-test)`. It does not need
  capnp includes beyond what lsp_types.h pulls (`kj/map.h`, `kj/string.h`).

### T2: JSON-RPC envelope + framing → new src/json_rpc.{h,cpp}

- Create `src/json_rpc.h/.cpp`. Move from `lsp_message_handler.cpp`, converting
  from private methods to free functions (same bodies):
  - `buildResponseString(double id, const capnp::JsonValue::Reader&)`
    (line 312)
  - `buildErrorResponseString(double id, int code, kj::StringPtr message)`
    (line 362)
- Both currently end with identical Content-Length framing. Add ONE new free
  function `kj::String frameLspMessage(kj::StringPtr json)` returning
  `kj::str(LSP_CONTENT_LENGTH_HEADER, json.size(), LSP_HEADER_DELIMITER, json)`
  and use it in both (this is the only allowed body change).
- Update `lsp_message_handler.{h,cpp}`: remove the two private methods, call
  the free functions; remove now-unused includes if the compiler flags them.
- New test `tests/json_rpc_test.cpp` + CMake target
  `capnp-ls-json-rpc-test` (links `src/json_rpc.cpp`, `src/lsp_types.cpp`;
  needs `capnp-json`, `capnp`, `kj`): assert exact output strings for
  - a response with a string result;
  - a response whose result param is an empty object → `"result":null`;
  - an error response (code/message present, header framing correct).

### T3: LSP param parsing + location serialization → new src/lsp_json.{h,cpp}

- Create `src/lsp_json.h/.cpp`. Move verbatim from the anonymous namespace of
  `lsp_message_handler.cpp` (make them declared free functions; keep
  `TextDocumentPosition` struct in the header):
  - `struct TextDocumentPosition` (line 21)
  - `setPosition` (line 28), `setRange` (line 36), `setLocation` (line 44)
  - `parseTextDocumentPosition` (line 52), `parseTextDocumentPath` (line 81)
- `lsp_message_handler.cpp` keeps no anonymous-namespace helpers after T1–T3.
- New test `tests/lsp_json_test.cpp` + target `capnp-ls-lsp-json-test`
  (links `src/lsp_json.cpp`, `src/lsp_types.cpp`, `src/utils.cpp`; libs
  `capnp-json`, `capnp`, `kj`): build a JsonValue params object in the test,
  verify the 1-based internal convention: LSP `{line:5, character:3}` parses
  to `line==6 && character==4`; `setPosition` of `{6,4}` serializes back to
  `5`/`3`. Note in a comment that internal positions are 1-based and the wire
  is 0-based — this is the single place that convention is enforced.

**GATE 1 — stop, planner reviews phase diff.**

---

## Phase 2 — SymbolIndex: one owner for the six maps

### T4: Introduce `struct SymbolIndex` (new src/symbol_index.{h,cpp})

- Header: a struct holding exactly the six maps currently in
  `LspMessageHandler` (lsp_message_handler.h:91-96), same member names:
  `fileSourceInfoMap`, `nodeLocationMap`, `nodeMetadataMap`, `referenceMap`,
  `documentSymbolMap`, `diagnosticMap`. Add `KJ_DISALLOW_COPY`.
- Methods (move logic, bodies verbatim):
  - `void clear()` ← `LspMessageHandler::clearCompilationState` (.cpp:254)
  - `kj::Maybe<uint64_t> findNodeIdAtPosition(kj::StringPtr path, uint32_t line, uint32_t character)`
    ← (.cpp:558). Uses `containsPosition`/`isTighterRange` from T1.
- `LspMessageHandler` replaces the six members with `SymbolIndex index;` and
  delegates; all call sites switch to `index.fileSourceInfoMap` etc. — this is
  mechanical. Keep `clearCompilationState()` as a one-line wrapper calling
  `index.clear()` (reloadProjectConfig uses it).
- CMake: add `src/symbol_index.cpp` to `capnp-ls` and both existing test
  targets.

### T5: Collapse parameter threading onto SymbolIndex

- `CompilationManager::CompileParams` (compilation_manager.h:23): replace the
  six map reference members/ctor params with a single `SymbolIndex &index`.
  Keep `importPaths`, `fileName`, `workingDir`.
- `SymbolResolver::resolve` (symbol_resolver.h/.cpp:337): replace the five map
  parameters with `SymbolIndex &index` (note: resolve does not take
  diagnosticMap — `LinkedCompiler::compile` does; leave LinkedCompiler's
  signature alone, pass `index.diagnosticMap` at the call site in
  `compilation_manager.cpp`).
- Update call sites: `lsp_message_handler.cpp` (`compileCapnpFile`),
  `compilation_manager.cpp`, and `tests/linked_compiler_test.cpp` /
  `tests/initialize_config_test.cpp` (tests construct the maps locally today —
  construct one `SymbolIndex` instead; assertions then read `index.<map>`).
- No behavior change; the diff should be almost entirely signatures and
  member-access prefixes.

### T6: Split `SymbolResolver::resolve` into named steps

- `resolve` (symbol_resolver.cpp:337-507) is one ~170-line function with three
  sequential passes. Extract three file-local (anonymous namespace) helpers,
  bodies moved verbatim, all taking `SymbolIndex &index`,
  `PositionCalculator&`, and the resolveFilePath callback as needed:
  1. `clearStalePerFileState(...)` ← the first `for (auto node : ...)` loop
     (lines 394-414, including its comment block);
  2. `indexFileNode(...)` ← the FILE branch of the second loop (418-451);
  3. `indexDeclarationNode(...)` ← the non-FILE remainder (454-486).
- Delete the commented-out `KJ_LOG` debug blocks (488-501).
- `resolve` becomes: build the lookup maps, loop calling the helpers, catch.
- Existing `linked-compiler` ctest already covers this path; it must stay
  green. No new test required.

**GATE 2 — stop, planner reviews phase diff.**

---

## Phase 3 — slim the handler

### T7: Diagnostics publishing → new src/diagnostic_publisher.{h,cpp}

- Create class `DiagnosticPublisher` with ctor
  `DiagnosticPublisher(StdoutWriter &writer)`.
- Move from `LspMessageHandler` (bodies verbatim):
  - `publishDiagnostics(...)` (.cpp:431) — takes the diagnosticMap (pass
    `const SymbolIndex &` or just the map reference, executor's choice —
    prefer the map reference to keep coupling minimal) plus `workspacePath`
    as a `kj::StringPtr` parameter instead of reading the member;
  - `publishDiagnosticsForFile(...)` (.cpp:463) — replace its hand-rolled
    Content-Length framing tail with `frameLspMessage` from T2 (only allowed
    body change).
- `LspMessageHandler` holds a `DiagnosticPublisher` member and forwards from
  `compileCapnpFile`'s `.then`.
- CMake: add to `capnp-ls` and `capnp-ls-initialize-config-test`.

### T8: Workspace/config state → new src/workspace_state.{h,cpp}

- Create `struct WorkspaceState` owning: `workspacePath`, `importPaths`,
  `importPathsConfiguredByInitialization`, `defaultLogLevel`,
  `currentLogLevel` (moved from lsp_message_handler.h:97-101).
- Move methods (bodies verbatim): `reloadProjectConfig()` (.cpp:268) — it
  calls `clearCompilationState()`, so give it the signature
  `bool reloadProjectConfig(SymbolIndex &index)`; `isProjectConfigPath()`
  (.cpp:263) becomes a free function or static member.
- `LspMessageHandler` keeps a `WorkspaceState workspace;` member; the
  `CAPNP_LS_TESTING` accessors in lsp_message_handler.h:27-48 keep their
  signatures but read through `workspace.` (so
  `tests/initialize_config_test.cpp` does not change).
- `handleInitialize` writes `workspace.workspacePath` / `workspace.importPaths`.

### T9: Dispatch readability in handleMessage

- Extract from `handleMessage` (.cpp:131) a file-local struct + function:
  `struct JsonRpcRequest { kj::StringPtr method; kj::Maybe<double> id; capnp::JsonValue::Reader params; }`
  and `parseJsonRpcEnvelope(capnp::JsonValue::Reader root) -> JsonRpcRequest`
  ← the field-scan loop (lines 153-172).
- Extract the switch into a private method
  `kj::Promise<void> dispatch(LspMethod method, const capnp::JsonValue::Reader &params, capnp::MallocMessageBuilder &response)`.
- `handleMessage` keeps: header split, decode, envelope parse, dispatch,
  response/ID plumbing. Target: under ~80 lines.

**GATE 3 — stop, planner reviews phase diff.**

---

## Phase 4 — small cleanups (independent; can run in any order)

### T10: Dead code removal

- Delete `struct CompileError` (lsp_types.h:114) — zero references.
- Delete `CompilationManager::format` declaration + `FormatParams`
  (compilation_manager.h:55-61) — declared, never defined, never called.
  Keep `handleFormatting`'s TODO comment but reword to not reference the
  deleted API: `// TODO: implement formatting`.
- `CompilationManager` ctor takes `kj::AsyncIoContext&` and ignores it
  (compilation_manager.cpp:12-14). Remove the parameter and the
  `context.getIoContext()` call in the `LspMessageHandler` ctor. If
  `ServerContext::getIoContext` then has no callers, remove it too.

### T11: Single filesystem instance

- `kj::newDiskFilesystem()` is created per call in
  `PositionCalculator::buildLineTable` (symbol_resolver.cpp:72),
  `extractFilePath` (symbol_resolver.cpp:278), and
  `LinkedCompiler::compile` (linked_compiler.cpp:221).
- Thread one `kj::Filesystem&` from `CompilationManager::compile` (create it
  there, or as a CompilationManager member) down through
  `LinkedCompiler::compile`, `SymbolResolver::resolve`, `extractFilePath`,
  and `PositionCalculator` (ctor parameter). Pure parameter threading; no
  logic change. Update both test files' direct calls.

### T12 (optional, planner pre-approval required before starting):
unify diagnostic position convention

- `publishDiagnosticsForFile` emits `diagnostic.range` untranslated (compiler
  SourcePos, 0-based), while every other range goes through `setPosition`
  (internal 1-based → wire 0-based). It works, but the convention is implicit.
  Proposal: convert diagnostics to the internal 1-based convention in
  `DiagnosticReporter::addError` (+1) and emit them through `setRange` from
  T3. **Behavior-visible if done wrong** — requires a characterization test
  first (capture a publishDiagnostics payload for a fixture with a known
  error, byte-compare before/after). Skip unless explicitly approved.

**GATE 4 — final planner review of full diff; planner runs `just test` and
spot-checks the LSP server against `samples/`.**

---

## Out of scope (noted, not for this refactor)

- `uriToPath` (utils.cpp) does not percent-decode URIs (breaks on paths with
  spaces) — behavior bug, file separately.
- `initialize` response advertises `workspace/didChangeWatchedFiles` as a
  top-level capability key, which is not a real `ServerCapabilities` field —
  harmless but wrong; file separately.
- Async/debounced compilation in `CompilationManager` (the reason ioContext
  existed) — feature work, not refactoring.

## Task → file map (quick reference)

| Task | New files | Touched files |
|------|-----------|---------------|
| T1 | tests/lsp_types_test.cpp | lsp_types.{h,cpp}, lsp_message_handler.cpp, CMakeLists.txt |
| T2 | src/json_rpc.{h,cpp}, tests/json_rpc_test.cpp | lsp_message_handler.{h,cpp}, CMakeLists.txt |
| T3 | src/lsp_json.{h,cpp}, tests/lsp_json_test.cpp | lsp_message_handler.cpp, CMakeLists.txt |
| T4 | src/symbol_index.{h,cpp} | lsp_message_handler.{h,cpp}, CMakeLists.txt |
| T5 | — | compilation_manager.{h,cpp}, symbol_resolver.{h,cpp}, lsp_message_handler.cpp, both tests |
| T6 | — | symbol_resolver.cpp |
| T7 | src/diagnostic_publisher.{h,cpp} | lsp_message_handler.{h,cpp}, CMakeLists.txt |
| T8 | src/workspace_state.{h,cpp} | lsp_message_handler.{h,cpp}, CMakeLists.txt |
| T9 | — | lsp_message_handler.{h,cpp} |
| T10 | — | lsp_types.h, compilation_manager.{h,cpp}, lsp_message_handler.cpp, server_context.h |
| T11 | — | symbol_resolver.{h,cpp}, linked_compiler.{h,cpp}, compilation_manager.cpp, both tests |
| T12 | — | linked_compiler.cpp, diagnostic_publisher.cpp, tests |
