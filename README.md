# Cap'n Proto Language Server

A language server that provides IDE features for Cap'n Proto schema files, including go-to-definition and automatic recompilation.

## Building from Source

### Prerequisites

- Supported OS: Linux, macOS
- CMake
- Cap'n Proto libraries (version 1.1.0 or higher)

### Build Instructions

You have two options for building the language server:

#### Option 1: Using System Cap'n Proto

If you already have Cap'n Proto installed on your system:

```bash
cmake -B build .
cmake --build build
```

With this option, you must specify the `compilerPath` in the initialization options.

#### Option 2: Using Bundled Cap'n Proto

If you don't have Cap'n Proto installed or prefer a self-contained build:

```bash
cmake -B build -DUSE_BUNDLED_CAPNP_TOOL=ON .
cmake --build build
```

With this option, the `compilerPath` in the initialization options becomes optional.

This automatically downloads and installs Cap'n Proto, which is guaranteed to be compatible with the language server.

The executable for the language server is located at `build/capnp-ls` in both cases.

## Language Server Protocol Support

### Initialization

The language server requires the following initialization options:

```json
{
  "initializationOptions": {
    "capnp": {
      "compilerPath": "/path/to/capnp",
      "importPaths": [
        "path/to/schema/imports"
      ]
    }
  }
}
```
Required fields:
- `compilerPath`: The path to the Cap'n Proto compiler executable.
  - When built with `-DUSE_BUNDLED_CAPNP_TOOL=ON`, this path is optional as the bundled compiler will be used by default.
- `importPaths`: An array of import paths for Cap'n Proto schemas.
  - When multiple import paths are provided, they are searched in the specified order, similar to how the Cap'n Proto compiler operates.

### Go to Definition

- Enables navigation to the definition of types, enums, and other symbols in Cap'n Proto schema files.

### File Watching

- Automatically recompiles schemas when files are saved.

## Current Limitations

- Symbol resolution for imports (e.g., `import "/common.capnp"`) currently requires opening the imported file first.
- Limited support for single workspace folders.

## Upcoming Features

- Autocomplete feature that includes ordinals
- Formatting feature
- Windows support

## Sample VSCode Extension

The `samples` directory contains a complete VSCode extension that demonstrates how to use this language server. For details about the extension, see [samples/README.md](samples/README.md).

### Building and Running the Sample

1. Build the language server (see above)
2. Set up the extension:
   ```bash
   cd samples
   npm install
   npm run compile
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