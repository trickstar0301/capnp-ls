# Cap'n Proto Language Server

A language server that provides IDE features for Cap'n Proto schema files, including go-to-definition and automatic recompilation.

## Installing

Install the latest release binary:

```bash
curl -fsSL https://raw.githubusercontent.com/trickstar0301/capnp-ls/main/install.sh | sh
```

Or with `wget`:

```bash
wget -qO- https://raw.githubusercontent.com/trickstar0301/capnp-ls/main/install.sh | sh
```

By default, the installer downloads the `capnp-v1` binary into
`$HOME/.local/bin`. Override the release version, install directory, or linked
Cap'n Proto channel with environment variables:

```bash
curl -fsSL https://raw.githubusercontent.com/trickstar0301/capnp-ls/main/install.sh \
  | CAPNP_LS_VERSION=v0.0.2 \
    CAPNP_LS_INSTALL_DIR="$HOME/.local/bin" \
    CAPNP_LS_CAPNP_VERSION=v2 \
    sh
```

The installer verifies the release asset against its `.sha256` file before
placing the executable on your `PATH`.

## Claude Code and Copilot Plugins

The Claude Code and Copilot plugins do not bundle the language server binary.
Install `capnp-ls` first, then enable a plugin that starts:

```json
{
  "command": "capnp-ls",
  "args": ["--stdio"]
}
```

Plugin marketplace definitions are maintained separately from this repository so
one marketplace can host all `trickstar0301` plugins.

## Building from Source

### Prerequisites

- Supported OS: Linux, macOS arm64
- CMake
- Cap'n Proto C++ source tree
- A C++17-capable compiler for Cap'n Proto `v1`
- A C++23-capable compiler and standard library for Cap'n Proto `v2`

### Build Instructions

Build the language server against a Cap'n Proto C++ source checkout:

```bash
cmake -B build -DCAPNP_SOURCE_DIR=/path/to/capnproto/c++ .
cmake --build build
```

When building against Cap'n Proto `v2`, pass C++23 explicitly:

```bash
cmake -B build -DCMAKE_CXX_STANDARD=23 -DCAPNP_SOURCE_DIR=/path/to/capnproto/c++ .
cmake --build build
```

The linked compiler backend builds and links Cap'n Proto from the selected source
tree. To support another Cap'n Proto version, build the same `capnp-ls` source
with `CAPNP_SOURCE_DIR` pointing at that version's C++ source tree.

Supported Cap'n Proto source channels:

- `v1` (built and tested against `v1.4.0`)
- `v2`

The executable for the language server is located at `build/capnp-ls`.

## Release Artifacts

Release artifacts are versioned by the Cap'n Proto compatibility channel. Each
artifact is published with a matching `.sha256` file. The `capnp-v1` artifacts
are built and tested against Cap'n Proto `v1.4.0`. The `capnp-v2` artifacts are
built and tested against the Cap'n Proto `v2` branch.

Linux x86_64 artifacts are built by CI:

- `capnp-ls-linux-x86_64-capnp-v1`
- `capnp-ls-linux-x86_64-capnp-v2`

macOS arm64 artifacts are built locally by a maintainer and uploaded manually to
the GitHub release:

- `capnp-ls-macos-arm64-capnp-v1`
- `capnp-ls-macos-arm64-capnp-v2`

To build a macOS arm64 release artifact locally:

```bash
scripts/build-macos-arm64-release.sh v1 /path/to/capnproto-v1.4.0/c++
gh release upload \
  v0.0.2 \
  dist/capnp-ls-macos-arm64-capnp-v1 \
  dist/capnp-ls-macos-arm64-capnp-v1.sha256 \
  --clobber
```

## Project Configuration

Project-specific schema settings live in `.capnp-ls.json` at the workspace root.
Commit this file with your schemas so every editor and agent uses the same
Cap'n Proto import paths:

```json
{
  "importPaths": [
    "schemas/common",
    "vendor/capnp"
  ]
}
```

Fields:

- `importPaths`: An array of import paths for Cap'n Proto schemas.
  - Relative paths are resolved from the workspace root.
  - When multiple import paths are provided, they are searched in the specified order, similar to how the Cap'n Proto compiler operates.

Clients should initialize the language server with either `workspaceFolders` or
`rootUri` so `capnp-ls` can locate the workspace root. For Neovim and similar
clients, use `.capnp-ls.json` or `.git` as the root marker.

When the client sends watched-file notifications for `.capnp-ls.json`, the
server reloads the config without a restart. Invalid JSON keeps the last valid
configuration; deleting `.capnp-ls.json` clears project import paths.

## Language Server Protocol Support

### Initialization

LSP clients may still pass initialization options explicitly. When
`initializationOptions.capnp.importPaths` is present, it takes precedence over
`.capnp-ls.json`:

```json
{
  "initializationOptions": {
    "capnp": {
      "importPaths": [
        "path/to/schema/imports"
      ]
    }
  }
}
```

For clients that support project-level LSP configuration, the equivalent
initialization options are:

```json
{
  "capnp": {
    "importPaths": [
      "schemas/common",
      "vendor/capnp"
    ]
  }
}
```

### Go to Definition

- Enables navigation to the definition of types, enums, and other symbols in Cap'n Proto schema files.

### File Watching

- Automatically recompiles schemas when files are saved.

## Current Limitations

- Limited support for single workspace folders.

## Upcoming Features

- Autocomplete feature that includes ordinals
- Formatting feature

## Sample VSCode Extension

The `samples` directory contains a complete VSCode extension that demonstrates how to use this language server. For details about the extension, see [samples/README.md](samples/README.md).

### Building and Running the Sample

1. Build the language server (see above)
2. Set up the extension:
   ```bash
   cd samples
   pnpm install
   pnpm compile
   ```
3. Launch the extension in debug mode:
   - Run "Launch Client" from the Run/Debug view

### Customizing the Workspace

You can customize the sample workspace by modifying the second argument in the launch configuration's `args` array:

```json
"args": [
    "--extensionDevelopmentPath=${workspaceRoot}/samples",
    "/absolute/path/to/your/workspace"  // Change this path
]
```
