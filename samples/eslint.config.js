const tsParser = require('@typescript-eslint/parser');
const tsPlugin = require('@typescript-eslint/eslint-plugin');

module.exports = [
	{
		ignores: [
			'client/out/**',
			'client/node_modules/**',
			'node_modules/**',
			'.vscode-test/**'
		]
	},
	{
		files: ['client/src/**/*.ts', 'client/src/**/*.tsx'],
		languageOptions: {
			parser: tsParser,
			parserOptions: {
				ecmaVersion: 2020,
				sourceType: 'commonjs'
			}
		},
		plugins: {
			'@typescript-eslint': tsPlugin
		},
		rules: {}
	}
];
