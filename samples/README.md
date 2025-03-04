## Sample Client

### Prerequisites

Before running the sample client, you need to build the Cap'n Proto Language Server first:

```bash
# From the root directory of the project
cmake -B build .
cmake --build build
```

The language server executable will be available at `build/capnp-ls`. Make sure this build is successful before proceeding with the sample client setup.

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

To customize the client settings, edit the `.vscode/settings.json` file in your workspace as follows:

```json
{
    "capnproto.languageServer.path": "path/to/capnp-ls",
    "capnproto.compiler.path": "capnp",
    "capnproto.compiler.importPaths": [
        "path/to/schema/imports"
    ]
}
```

**Reference:** `samples/client/testFixture/.vscode/settings.json`.
