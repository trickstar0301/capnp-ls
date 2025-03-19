// Copyright (c) 2024 Atsushi Tomida
// 
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as path from 'path';
import { workspace, ExtensionContext, window } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
	const workspaceFolders = workspace.workspaceFolders;
    if (!workspaceFolders) {
        window.showErrorMessage('No workspace folder is opened');
        return;
    }
    const workspaceFolder = workspaceFolders[0];
	// Get configuration
	const config = workspace.getConfiguration('capnp-ls-client');
	const serverPathRaw = config.get<string>('languageServer.path');
	const compilerPathRaw = config.get<string>('compiler.path');
	const importPathsRaw = config.get<string[]>('compiler.importPaths') || [];
	const extraEnv = config.get<Record<string, string | number>>('server.extraEnv') || {};

	// Resolve environment variables in paths
	const serverPath = resolveEnvVars(serverPathRaw || '');
	const compilerPath = resolveEnvVars(compilerPathRaw || '');
	const resolvedImportPaths = importPathsRaw.map(p => resolveEnvVars(p));

	// Helper function to resolve environment variables in paths
	function resolveEnvVars(pathStr: string): string {
		if (!pathStr) return pathStr;
		
		// Replace both Unix and Windows style environment variables
		return pathStr.replace(/\$([a-zA-Z0-9_]+)|\$\{([a-zA-Z0-9_]+)\}|%([a-zA-Z0-9_]+)%/g, 
			(match, unixVar, unixBracedVar, windowsVar) => {
				const varName = unixVar || unixBracedVar || windowsVar;
				const envValue = process.env[varName];
				return envValue || match; // Keep original if not found
			});
	}

	// Resolve server path
	const resolvedServerPath = path.isAbsolute(serverPath) 
		? serverPath 
		: context.asAbsolutePath(serverPath);

	// Server options
	const serverOptions: ServerOptions = {
		command: resolvedServerPath,
		args: [],
		options: {
			cwd: path.dirname(resolvedServerPath),
			env: {
				...process.env,
				...extraEnv
			}
		}
	};

	// Create output channels
	const outputChannel = window.createOutputChannel('Cap\'n Proto LSP');

	// Logger function
	function log(message: string): void {
		outputChannel.appendLine(`[Client] ${message}`);
	}

	// Log configuration information
	log(`Server path: ${resolvedServerPath}`);
	log(`Compiler path: ${compilerPath}`);
	log(`Import paths: ${resolvedImportPaths.join(', ')}`);

	// Client options
	const clientOptions: LanguageClientOptions = {
		documentSelector: [{ scheme: 'file', language: 'capnp' }],
		synchronize: {
			fileEvents: workspace.createFileSystemWatcher('**/*.capnp')
		},
		outputChannel: outputChannel,
		workspaceFolder: workspaceFolder,
		initializationOptions: {
			capnp: {
				compilerPath: compilerPath,
				importPaths: resolvedImportPaths
			}
		},
		middleware: {
			provideDefinition: (document, position, token, next) => {
				log(`Definition requested at position: ${position.line}:${position.character}`);
				return next(document, position, token);
			}
		}
	};

	// Create and start the client
	client = new LanguageClient(
		'capnproto-language-server',
		'Cap\'n Proto Language Server',
		serverOptions,
		clientOptions
	);

	// outputChannel.show();

	client.start();
}

export function deactivate(): Thenable<void> | undefined {
	console.log('Cap\'n Proto Language Server extension deactivating...');
	
	if (!client) {
	  return undefined;
	}
	
	const timeout = new Promise<void>((resolve, reject) => {
	  const id = setTimeout(() => {
		clearTimeout(id);
		console.log('Client stop timed out, forcing shutdown');
		resolve();
	  }, 5000);
	});
	
	return Promise.race([
	  client.stop(),
	  timeout
	]);
  }