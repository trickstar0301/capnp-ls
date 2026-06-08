// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as assert from 'assert';
import { createServerEnvironment } from '../extension';

suite('Server Environment', () => {
	test('defaults CPP_LOG to warning instead of inheriting parent CPP_LOG', () => {
		const env = createServerEnvironment(
			{ CPP_LOG: 'lsp_server=info', PATH: '/bin' },
			{}
		);

		assert.strictEqual(env.CPP_LOG, 'lsp_server=warning');
		assert.strictEqual(env.PATH, '/bin');
	});

	test('allows extraEnv to override default CPP_LOG', () => {
		const env = createServerEnvironment(
			{ CPP_LOG: 'lsp_server=warning' },
			{ CPP_LOG: 'lsp_server=info' }
		);

		assert.strictEqual(env.CPP_LOG, 'lsp_server=info');
	});
});
