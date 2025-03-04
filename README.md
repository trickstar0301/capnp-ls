# Cap'n Proto Language Server

## Building from Source

### Prerequisites

- Supported OS: Linux, macOS
- CMake
- Cap'n Proto libraries

### Build Instructions

```bash
cmake -B build .
cmake --build build
```
The executable for the language server is located at `build/capnp-ls`.

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
- `importPaths`: An array of import paths for Cap'n Proto schemas (relative to the workspace root).
  - When multiple import paths are provided, they are searched in the specified order, similar to how the Cap'n Proto compiler operates.

### Go to Definition

- Enables navigation to the definition of types, enums, and other symbols in Cap'n Proto schema files.

### File Watching

- Automatically recompiles schemas when files are saved.

## Current Limitations

- Symbol resolution for imports (e.g., `import "/common.capnp"`) currently requires opening the imported file first.
- Limited support for single workspace folders.

## Upcoming Features
- Compile error messages
- Linting support
- Windows support