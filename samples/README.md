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

* `capnp-ls-client.languageServer.path`: Path to the Cap'n Proto language server executable.
* `capnp-ls-client.compiler.path`: Path to the Cap'n Proto compiler executable.
* `capnp-ls-client.compiler.importPaths`: Additional import paths for Cap'n Proto schemas.
* `capnp-ls-client.server.extraEnv`: Extra environment variables that will be passed to the capnp-ls executable.
  * `CPP_LOG`: Log level for the Cap'n Proto language server.
    * Example: `CPP_LOG=lsp_server=info`: Set log level to info.
    * Default: `CPP_LOG=lsp_server=warning`

#### Example configuration:

To customize the client settings, edit the `.vscode/settings.json` file in your workspace as follows:

```json
{
    "capnp-ls-client.languageServer.path": "/absolute/path/to/capnp-ls",
    "capnp-ls-client.compiler.path": "capnp",
    "capnp-ls-client.compiler.importPaths": [
        "path/to/schema/imports"
    ],
    "capnp-ls-client.server.extraEnv": {
        "CPP_LOG": "lsp_server=warning"
    }
}
```
See [example configuration](https://github.com/trickstar0301/capnp-ls/blob/main/samples/client/testFixture/.vscode/settings.json) for more details.