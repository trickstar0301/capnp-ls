// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as path from 'path';
import * as fs from 'fs';
import * as crypto from 'crypto';
import { workspace, ExtensionContext, window, commands } from 'vscode';
import * as https from 'https';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient;

type ReleasePlatform = 'linux-x86_64' | 'macos-arm64';
type ServerExtraEnv = Record<string, string | number>;

export function createServerEnvironment(
	baseEnv: NodeJS.ProcessEnv,
	extraEnv: ServerExtraEnv
): NodeJS.ProcessEnv {
	return {
		...baseEnv,
		CPP_LOG: 'lsp_server=warning',
		...Object.fromEntries(
			Object.entries(extraEnv).map(([key, value]) => [key, String(value)])
		)
	};
}

export async function activate(context: ExtensionContext) {
	if (process.platform === 'win32') {
		window.showErrorMessage('Windows is not supported by Cap\'n Proto Language Server.');
		return;
	}
	if (process.platform === 'darwin' && process.arch !== 'arm64') {
		window.showErrorMessage('Only macOS arm64 is supported by Cap\'n Proto Language Server.');
		return;
	}

	context.subscriptions.push(
		workspace.onDidChangeConfiguration(async event => {
			if (
				event.affectsConfiguration('capnp-ls-client.languageServer.capnpChannel') ||
				event.affectsConfiguration('capnp-ls-client.languageServer.version') ||
				event.affectsConfiguration('capnp-ls-client.languageServer.path')
			) {
				const choice = await window.showInformationMessage(
					'Cap\'n Proto language server settings changed. Reload VS Code to apply the new server version.',
					'Reload Window',
					'Later'
				);

				if (choice === 'Reload Window') {
					await commands.executeCommand('workbench.action.reloadWindow');
				}
			}
		})
	);

	const workspaceFolders = workspace.workspaceFolders;
    if (!workspaceFolders) {
        window.showErrorMessage('No workspace folder is opened');
        return;
    }
    const workspaceFolder = workspaceFolders[0];
	// Get configuration
	const config = workspace.getConfiguration('capnp-ls-client');
	const serverPathRaw = config.get<string>('languageServer.path');
	const extraEnv = config.get<ServerExtraEnv>('server.extraEnv') || {};
	const serverVersion = config.get<string>('languageServer.version');
	const capnpChannel = config.get<string>('languageServer.capnpChannel');
	if (!serverVersion || !capnpChannel) {
		window.showErrorMessage('Cap\'n Proto language server version or channel is not configured.');
		return;
	}

	// Helper function to resolve environment variables in paths
	function resolveEnvVars(pathStr: string): string {
		if (!pathStr) return pathStr;

		// Replace Unix-style environment variables.
		return pathStr.replace(/\$([a-zA-Z0-9_]+)|\$\{([a-zA-Z0-9_]+)\}/g,
			(match, unixVar, unixBracedVar) => {
				const varName = unixVar || unixBracedVar;
				const envValue = process.env[varName];
				return envValue || match; // Keep original if not found
			});
	}

	const outputChannel = window.createOutputChannel('Cap\'n Proto LSP', { log: true });

	function log(message: string): void {
		outputChannel.appendLine(`[Client] ${message}`);
	}

	function errorMessage(error: unknown): string {
		return error instanceof Error ? error.message : String(error);
	}

	function getReleasePlatform(): ReleasePlatform | undefined {
		if (process.platform === 'linux' && process.arch === 'x64') {
			return 'linux-x86_64';
		}
		if (process.platform === 'darwin' && process.arch === 'arm64') {
			return 'macos-arm64';
		}
		return undefined;
	}

	let resolvedServerPath: string;

	if (serverPathRaw) {
		const serverPath = resolveEnvVars(serverPathRaw);
		resolvedServerPath = path.isAbsolute(serverPath)
			? serverPath
			: context.asAbsolutePath(serverPath);
	} else {
		try {
			resolvedServerPath = await findLanguageServer(context, serverVersion, capnpChannel);
		} catch (err) {
			const message = errorMessage(err);
			log(message);
			window.showErrorMessage(message);
			return;
		}
	}

	function normalizeCapnpChannel(capnpChannel: string): string {
		switch (capnpChannel) {
			case 'v1':
				return 'v1';
			case 'v2':
				return 'v2';
			default:
				return capnpChannel;
		}
	}

	async function findLanguageServer(context: ExtensionContext, version: string, capnpChannel: string): Promise<string> {
		const extensionPath = context.extensionPath;
		const normalizedCapnpChannel = normalizeCapnpChannel(capnpChannel);
		const versionedBinaryName = `capnp-ls-capnp-${normalizedCapnpChannel}`;
		const extensionVersionedBinaryPath = path.join(extensionPath, versionedBinaryName);

		if (fs.existsSync(extensionVersionedBinaryPath)) {
			try {
				fs.accessSync(extensionVersionedBinaryPath, fs.constants.X_OK);
				log(`Found language server at: ${extensionVersionedBinaryPath}`);
				return extensionVersionedBinaryPath;
			} catch (e) {
				try {
					await fs.promises.chmod(extensionVersionedBinaryPath, 0o755);
					fs.accessSync(extensionVersionedBinaryPath, fs.constants.X_OK);
					log(`Fixed language server executable permission at: ${extensionVersionedBinaryPath}`);
					return extensionVersionedBinaryPath;
				} catch {
					throw new Error(`Language server is not executable: ${extensionVersionedBinaryPath}`);
				}
			}
		}

		const releasePlatform = getReleasePlatform();
		if (releasePlatform && !fs.existsSync(extensionVersionedBinaryPath)) {
			log(`Binary not found at ${extensionVersionedBinaryPath} and we're on ${releasePlatform}, attempting to download...`);
			try {
				await downloadCapnpLs(extensionVersionedBinaryPath, version, normalizedCapnpChannel, releasePlatform);
				log(`Successfully downloaded capnp-ls version ${version} for Cap'n Proto ${normalizedCapnpChannel} to ${extensionVersionedBinaryPath}`);
				return extensionVersionedBinaryPath;
			} catch (err) {
				log(`Failed to download capnp-ls: ${errorMessage(err)}`);
			}
		}

		throw new Error(
			`Could not find capnp-ls for Cap'n Proto ${normalizedCapnpChannel}. ` +
			`Set capnp-ls-client.languageServer.path explicitly or install ${versionedBinaryName} in the extension directory.`
		);
	}

	function downloadCapnpLs(targetPath: string, version: string, capnpChannel: string, platform: ReleasePlatform): Promise<void> {
		const assetName = `capnp-ls-${platform}-capnp-${capnpChannel}`;
		const url = `https://github.com/trickstar0301/capnp-ls/releases/download/${version}/${assetName}`;
		log(`Downloading capnp-ls version ${version} for Cap'n Proto ${capnpChannel} from ${url} to ${targetPath}`);
		return downloadFile(url, targetPath)
			.then(() => verifyDownloadedChecksum(targetPath, `${url}.sha256`))
			.then(() => fs.promises.chmod(targetPath, 0o755))
			.then(() => {
				log('Download completed, checksum verified, and file made executable');
			})
			.catch(err => {
				fs.unlink(targetPath, () => {});
				throw err;
			});
	}

	function requestUrl(url: string): Promise<Buffer> {
		return new Promise((resolve, reject) => {
			const request = https.get(url, {
				headers: {
					'User-Agent': 'VSCode-CapnProto-Extension',
					'Accept': 'application/octet-stream'
				}
			}, response => {
				if (response.statusCode === 301 || response.statusCode === 302) {
					const redirectUrl = response.headers.location;
					response.resume();
					if (!redirectUrl) {
						reject(new Error(`Redirect location not found: ${response.statusCode} ${response.statusMessage}`));
						return;
					}
					resolve(requestUrl(redirectUrl));
					return;
				}

				if (response.statusCode !== 200) {
					response.resume();
					reject(new Error(`Failed to download: ${response.statusCode} ${response.statusMessage}`));
					return;
				}

				const chunks: Buffer[] = [];
				response.on('data', chunk => chunks.push(Buffer.from(chunk)));
				response.on('end', () => resolve(Buffer.concat(chunks)));
				response.on('error', reject);
			}).on('error', reject);

			request.setTimeout(30000, () => {
				request.destroy();
				reject(new Error('Download timed out after 30 seconds'));
			});
		});
	}

	async function downloadFile(url: string, targetPath: string): Promise<void> {
		const data = await requestUrl(url);
		if (data.length === 0) {
			throw new Error('Downloaded file is empty');
		}
		await fs.promises.writeFile(targetPath, data, { mode: 0o644 });
		log(`Downloaded file size: ${data.length} bytes`);
	}

	async function verifyDownloadedChecksum(targetPath: string, checksumUrl: string): Promise<void> {
		const checksumText = (await requestUrl(checksumUrl)).toString('utf8');
		const expected = checksumText.trim().split(/\s+/)[0];
		const file = await fs.promises.readFile(targetPath);
		const actual = crypto.createHash('sha256').update(file).digest('hex');
		if (expected !== actual) {
			throw new Error(`Checksum mismatch for ${targetPath}: expected ${expected}, got ${actual}`);
		}
		log('Checksum verified');
	}

	log(`Server path: ${resolvedServerPath}`);
	log(`Cap'n Proto channel: ${normalizeCapnpChannel(capnpChannel)}`);

		// Server options
	const serverOptions: ServerOptions = {
		command: resolvedServerPath,
		args: [],
		options: {
			cwd: path.dirname(resolvedServerPath),
			env: createServerEnvironment(process.env, extraEnv)
		}
	};

	const clientOptions: LanguageClientOptions = {
		documentSelector: [{ scheme: 'file', language: 'capnp' }],
		synchronize: {
			fileEvents: [
				workspace.createFileSystemWatcher('**/*.capnp'),
				workspace.createFileSystemWatcher('**/.capnp-ls.json')
			]
		},
		outputChannel: outputChannel,
		workspaceFolder: workspaceFolder,
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
