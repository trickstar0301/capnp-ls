# Cap'n Proto Language Support for VS Code

A VS Code extension that provides language support for Cap'n Proto schema files.

## Features

- Go to definition
- Diagnostics (error reporting)

## Requirements

- Cap'n Proto compiler(version 1.1.0 or higher): [capnp](https://capnproto.org/install.html)
- Cap'n Proto Language Server: [capnp-ls](https://github.com/trickstar0301/capnp-ls)

## Extension Settings

This extension contributes the following settings:

* `capnproto.languageServer.path`: Path to the Cap'n Proto language server executable.
* `capnproto.compiler.path`: Path to the Cap'n Proto compiler executable.
* `capnproto.compiler.importPaths`: Additional import paths for Cap'n Proto schemas. Relative to the workspace root.

#### Example configuration:

To customize the client settings, edit the `.vscode/settings.json` file in your workspace as follows:

```json
{
    "capnproto.languageServer.path": "absolute/path/to/capnp-ls",
    "capnproto.compiler.path": "capnp",
    "capnproto.compiler.importPaths": [
        "relative/path/from/workspace/root/to/schema/imports"
    ]
}
```
See [example configuration](https://github.com/trickstar0301/capnp-ls/blob/main/samples/client/testFixture/.vscode/settings.json) for more details.