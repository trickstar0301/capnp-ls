## Sample Client

### Building the Sample Client

To build the sample client, run the following commands:

```bash
cd samples
npm install
npm run compile
```

### Running the Sample Client

The sample client can be executed using VSCode's launch configurations:

1. Open the project in VSCode.
2. Select the **"Launch Client"** configuration from the Run/Debug view.
3. Press **F5** to start debugging.

#### Launch Configurations

The sample includes two launch configurations:

1. **Launch Client** - Runs the extension in development mode.
2. **Language Server E2E Test** - Runs the end-to-end tests.

#### Customizing the Workspace

You can customize the sample workspace by modifying the second argument in the launch configuration's `args` array:

```json
"args": [
    "--extensionDevelopmentPath=${workspaceRoot}/samples",
    "/absolute/path/to/your/workspace"  // Change this path
]
```

#### Customizing the `settings.json`

To configure the sample client, modify the `settings.json` file as follows:

```json
{
    "capnproto.languageServer.path": "path/to/capnp-ls",
    "capnproto.compiler.path": "capnp",
    "capnproto.compiler.importPaths": [
        "path/to/schema/imports"
    ]
}
```

**Reference:** See `samples/client/testFixture/.vscode/settings.json`.
