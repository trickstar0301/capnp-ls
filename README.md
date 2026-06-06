# Cap'n Proto Language Server

A language server that provides IDE features for Cap'n Proto schema files, including go-to-definition and automatic recompilation.

## Building from Source

### Prerequisites

- Supported OS: Linux, macOS arm64
- CMake
- Cap'n Proto C++ source tree
- A C++17-capable compiler for Cap'n Proto `v1.1.0` and `v1.2.0`
- A C++23-capable compiler and standard library for Cap'n Proto `v2` / `2.0-dev`

### Build Instructions

Build the language server against a Cap'n Proto C++ source checkout:

```bash
cmake -B build -DCAPNP_SOURCE_DIR=/path/to/capnproto/c++ .
cmake --build build
```

When building against Cap'n Proto `v2` / `2.0-dev`, pass C++23 explicitly:

```bash
cmake -B build -DCMAKE_CXX_STANDARD=23 -DCAPNP_SOURCE_DIR=/path/to/capnproto/c++ .
cmake --build build
```

The linked compiler backend builds and links Cap'n Proto from the selected source
tree. To support another Cap'n Proto version, build the same `capnp-ls` source
with `CAPNP_SOURCE_DIR` pointing at that version's C++ source tree.

Supported and tested Cap'n Proto source versions:

- `v1.1.0`
- `v1.2.0`
- `v2` / `2.0-dev`

The executable for the language server is located at `build/capnp-ls`.

## Release Artifacts

Release artifacts are versioned by the Cap'n Proto compiler source linked into
the server binary.

Linux x86_64 artifacts are built by CI:

- `capnp-ls-linux-x86_64-capnp-1.1.0`
- `capnp-ls-linux-x86_64-capnp-1.2.0`
- `capnp-ls-linux-x86_64-capnp-2.0-dev`

macOS is supported for arm64 only. macOS arm64 artifacts are built locally by a
maintainer and uploaded manually to the GitHub release:

- `capnp-ls-macos-arm64-capnp-1.1.0`
- `capnp-ls-macos-arm64-capnp-1.2.0`
- `capnp-ls-macos-arm64-capnp-2.0-dev`

To build a macOS arm64 release artifact locally:

```bash
scripts/build-macos-arm64-release.sh 1.2.0 /path/to/capnproto/c++
gh release upload v0.0.1 dist/capnp-ls-macos-arm64-capnp-1.2.0 --clobber
```

## Language Server Protocol Support

### Initialization

The language server requires the following initialization options:

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
Fields:
- `importPaths`: An array of import paths for Cap'n Proto schemas.
  - When multiple import paths are provided, they are searched in the specified order, similar to how the Cap'n Proto compiler operates.

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
