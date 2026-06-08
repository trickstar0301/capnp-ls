/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See License.txt in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
import * as path from 'path';
import * as fs from 'fs/promises';
import * as os from 'os';

import { runTests } from '@vscode/test-electron';

async function main() {
	try {
		// The folder containing the Extension Manifest package.json
		const extensionDevelopmentPath = path.resolve(__dirname, '../../../');
		console.log('Extension development path:', extensionDevelopmentPath);

		// The path to test runner
		const extensionTestsPath = path.resolve(__dirname, './index');
		console.log('Extension tests path:', extensionTestsPath);

		const testWorkspacePath = path.resolve(__dirname, '../../testFixture');
		console.log('Test workspace path:', testWorkspacePath);

		const userDataDir = await fs.mkdtemp(path.join(os.tmpdir(), 'capnp-ls-vscode-test-'));
		const userSettingsDir = path.join(userDataDir, 'User');
		await fs.mkdir(userSettingsDir, { recursive: true });
		await fs.writeFile(path.join(userSettingsDir, 'settings.json'), JSON.stringify({
			'capnp-ls-client.languageServer.path': path.resolve(extensionDevelopmentPath, '../build/capnp-ls'),
			'capnp-ls-client.languageServer.capnpChannel': 'v1'
		}, null, 2));

		// Download VS Code, unzip it and run the integration test
		await runTests({
			extensionDevelopmentPath,
			extensionTestsPath,
			launchArgs: [
				'--user-data-dir',
				userDataDir,
				testWorkspacePath
			]
		});
	} catch (err) {
		console.error('Failed to run tests:', err);
		process.exit(1);
	}
}

main();
