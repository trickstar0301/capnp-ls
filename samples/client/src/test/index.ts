/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */
import * as path from 'path';
import * as fs from 'fs/promises';
import Mocha = require('mocha');

async function findTestFiles(dir: string): Promise<string[]> {
	const entries = await fs.readdir(dir, { withFileTypes: true });
	const files: string[] = [];

	for (const entry of entries) {
		const fullPath = path.join(dir, entry.name);
		if (entry.isDirectory()) {
			files.push(...await findTestFiles(fullPath));
		} else if (entry.isFile() && entry.name.endsWith('.test.js')) {
			files.push(fullPath);
		}
	}

	return files;
}

export function run(): Promise<void> {
	// Create the mocha test
	const mocha = new Mocha({
		ui: 'tdd',
		color: true
	});
	mocha.timeout(200000);

	const testsRoot = __dirname;

	return new Promise((resolve, reject) => {
		findTestFiles(testsRoot).then(files => {
			// Add files to the test suite
			files.forEach(f => mocha.addFile(f));

			try {
				// Run the mocha test
				mocha.run(failures => {
					if (failures > 0) {
						reject(new Error(`${failures} tests failed.`));
					} else {
						resolve();
					}
				});
			} catch (err) {
				console.error(err);
				reject(err);
			}
		}).catch(reject);
	});
}
