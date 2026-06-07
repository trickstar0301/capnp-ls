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

* `capnp-ls-client.languageServer.path`: Path to the Cap'n Proto language server executable. If not specified, the extension uses the versioned binary for `capnp-ls-client.languageServer.capnpChannel` from the extension directory. For Linux x86_64 and macOS arm64 systems, that binary will be automatically downloaded if it is available in the configured GitHub release.
* `capnp-ls-client.languageServer.version`: capnp-ls release tag used for automatic Linux x86_64 and macOS arm64 downloads.
* `capnp-ls-client.languageServer.capnpChannel`: Cap'n Proto compatibility channel for the automatically downloaded language server binary. Supported values are `v1` and `v2`.
  * Changing the server path, release version, or Cap'n Proto channel prompts you to reload VS Code so the new server binary is used.
* `capnp-ls-client.server.extraEnv`: Extra environment variables that will be passed to the capnp-ls executable.
  * `CPP_LOG`: Log level for the Cap'n Proto language server.
    * Example: `CPP_LOG=lsp_server=info`: Set log level to info.
    * Default: `CPP_LOG=lsp_server=warning`

#### Example configuration:

Project-specific import paths are read by the language server from
`.capnp-ls.json` in your workspace root:

```json
{
    "importPaths": [
        "path/to/schema/imports"
    ]
}
```

To customize client settings, configure them in VS Code User Settings:

```json
{
    "capnp-ls-client.languageServer.path": "/absolute/path/to/capnp-ls",
    "capnp-ls-client.languageServer.capnpChannel": "v1",
    "capnp-ls-client.server.extraEnv": {
        "CPP_LOG": "lsp_server=warning"
    }
}
```
## Release Artifact Availability

Linux x86_64 release artifacts are built by CI. macOS arm64 release artifacts
are built locally by a maintainer and uploaded manually. The VS Code extension
uses the following asset names:

```text
capnp-ls-linux-x86_64-capnp-v1
capnp-ls-linux-x86_64-capnp-v2
capnp-ls-macos-arm64-capnp-v1
capnp-ls-macos-arm64-capnp-v2
```

Windows and macOS x86_64 are not supported.
