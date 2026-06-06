# Cap'n Proto Language Support for VS Code

A VS Code extension that provides language support for Cap'n Proto schema files.

## Features

- Go to definition
- Diagnostics (error reporting)

## Requirements

- Supported OS: Linux, macOS arm64
- Cap'n Proto Language Server: [capnp-ls](https://github.com/trickstar0301/capnp-ls)

## Extension Settings

This extension contributes the following settings:

* `capnp-ls-client.languageServer.path`: Path to the Cap'n Proto language server executable. If not specified, the extension uses the versioned binary for `capnp-ls-client.languageServer.capnpVersion` from the extension directory. For Linux x86_64 and macOS arm64 systems, that binary will be automatically downloaded if it is available in the configured GitHub release.
* `capnp-ls-client.languageServer.version`: capnp-ls release tag used for automatic Linux x86_64 and macOS arm64 downloads.
* `capnp-ls-client.languageServer.capnpVersion`: Cap'n Proto compiler version linked into the automatically downloaded language server binary. Supported values are `1.1.0`, `1.2.0`, and `2.0-dev`.
  * Changing the server path, release version, or linked Cap'n Proto version prompts you to reload VS Code so the new server binary is used.
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
    "capnp-ls-client.languageServer.capnpVersion": "1.2.0",
    "capnp-ls-client.compiler.importPaths": [
        "path/to/schema/imports"
    ],
    "capnp-ls-client.server.extraEnv": {
        "CPP_LOG": "lsp_server=warning"
    }
}
```
See [example configuration](https://github.com/trickstar0301/capnp-ls/blob/main/samples/client/testFixture/.vscode/settings.json) for more details.

## Release Artifact Availability

Linux x86_64 release artifacts are built by CI. macOS arm64 release artifacts
are built locally by a maintainer and uploaded manually. The VS Code extension
uses the following asset names:

```text
capnp-ls-linux-x86_64-capnp-1.2.0
capnp-ls-macos-arm64-capnp-1.2.0
```

Windows and macOS x86_64 are not supported.
