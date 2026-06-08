// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as assert from 'assert';
import { createServerEnvironment } from '../extension';

suite('Server Environment', () => {
	test('preserves the parent environment without adding CPP_LOG', () => {
		const env = createServerEnvironment(
			{ PATH: '/bin' },
			{}
		);

		assert.strictEqual(env.CPP_LOG, undefined);
		assert.strictEqual(env.PATH, '/bin');
	});

	test('allows extraEnv to add stringified values', () => {
		const env = createServerEnvironment(
			{ PATH: '/bin' },
			{ CAPNP_LS_TEST_VALUE: 123 }
		);

		assert.strictEqual(env.CAPNP_LS_TEST_VALUE, '123');
	});
});
