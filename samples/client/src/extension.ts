// Copyright (c) 2024 Atsushi Tomida
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as path from 'path';
import * as fs from 'fs';
import { workspace, ExtensionContext, window, commands } from 'vscode';
import * as https from 'https';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient;

type ReleasePlatform = 'linux-x86_64' | 'macos-arm64';

// Default language server version for downloadable release binaries.
const DEFAULT_SERVER_VERSION = 'v0.0.2';
const DEFAULT_CAPNP_VERSION = '1.2.0';

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
				event.affectsConfiguration('capnp-ls-client.languageServer.capnpVersion') ||
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
	const importPathsRaw = config.get<string[]>('compiler.importPaths') || [];
	const extraEnv = config.get<Record<string, string | number>>('server.extraEnv') || {};
	// Get server version from configuration or use default
	const serverVersion = config.get<string>('languageServer.version') || DEFAULT_SERVER_VERSION;
	const capnpVersion = config.get<string>('languageServer.capnpVersion') || DEFAULT_CAPNP_VERSION;

	// Resolve environment variables in paths
	const resolvedImportPaths = importPathsRaw.map(p => resolveEnvVars(p));

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
			resolvedServerPath = await findLanguageServer(context, serverVersion, capnpVersion);
		} catch (err) {
			const message = errorMessage(err);
			log(message);
			window.showErrorMessage(message);
			return;
		}
	}

	async function findLanguageServer(context: ExtensionContext, version: string, capnpVersion: string): Promise<string> {
		const extensionPath = context.extensionPath;
		const versionedBinaryName = `capnp-ls-capnp-${capnpVersion}`;
		const extensionVersionedBinaryPath = path.join(extensionPath, versionedBinaryName);

		if (fs.existsSync(extensionVersionedBinaryPath)) {
			try {
				fs.accessSync(extensionVersionedBinaryPath, fs.constants.X_OK);
				log(`Found language server at: ${extensionVersionedBinaryPath}`);
				return extensionVersionedBinaryPath;
			} catch (e) {
				throw new Error(`Language server is not executable: ${extensionVersionedBinaryPath}`);
			}
		}

		const releasePlatform = getReleasePlatform();
		if (releasePlatform && !fs.existsSync(extensionVersionedBinaryPath)) {
			log(`Binary not found at ${extensionVersionedBinaryPath} and we're on ${releasePlatform}, attempting to download...`);
			try {
				await downloadCapnpLs(extensionVersionedBinaryPath, version, capnpVersion, releasePlatform);
				log(`Successfully downloaded capnp-ls version ${version} for Cap'n Proto ${capnpVersion} to ${extensionVersionedBinaryPath}`);
				return extensionVersionedBinaryPath;
			} catch (err) {
				log(`Failed to download capnp-ls: ${errorMessage(err)}`);
			}
		}

		throw new Error(
			`Could not find capnp-ls linked with Cap'n Proto ${capnpVersion}. ` +
			`Set capnp-ls-client.languageServer.path explicitly or install ${versionedBinaryName} in the extension directory.`
		);
	}

	function downloadCapnpLs(targetPath: string, version: string, capnpVersion: string, platform: ReleasePlatform): Promise<void> {
		const assetName = `capnp-ls-${platform}-capnp-${capnpVersion}`;
		const url = `https://github.com/trickstar0301/capnp-ls/releases/download/${version}/${assetName}`;
		log(`Downloading capnp-ls version ${version} for Cap'n Proto ${capnpVersion} from ${url} to ${targetPath}`);

		return new Promise((resolve, reject) => {
			// Open the file only after the download response is accepted.
			let file: fs.WriteStream | null = null;
			
			// GitHub may require a User-Agent header.
			const options = {
				headers: {
					'User-Agent': 'VSCode-CapnProto-Extension',
					'Accept': 'application/octet-stream'
				},
				followRedirects: true
			};

			log(`Sending request with options: ${JSON.stringify(options)}`);

			const request = https.get(url, options, (response) => {
				log(`Received response with status code: ${response.statusCode}`);
				log(`Response headers: ${JSON.stringify(response.headers)}`);

				if (response.statusCode === 302 || response.statusCode === 301) {
					const redirectUrl = response.headers.location;
					if (!redirectUrl) {
						reject(new Error(`Redirect location not found: ${response.statusCode} ${response.statusMessage}`));
						return;
					}

					log(`Following redirect to: ${redirectUrl}`);

					request.destroy();

					const redirectUrlObj = new URL(redirectUrl);
					const redirectOptions = {
						host: redirectUrlObj.hostname,
						path: redirectUrlObj.pathname + redirectUrlObj.search,
						headers: {
							'User-Agent': 'VSCode-CapnProto-Extension',
							'Accept': 'application/octet-stream'
						}
					};

					log(`Sending redirect request with options: ${JSON.stringify(redirectOptions)}`);

					https.get(redirectOptions, (redirectResponse) => {
						log(`Redirect response status: ${redirectResponse.statusCode}`);
						log(`Redirect response headers: ${JSON.stringify(redirectResponse.headers)}`);

						if (redirectResponse.statusCode !== 200) {
							reject(new Error(`Failed to download from redirect: ${redirectResponse.statusCode} ${redirectResponse.statusMessage}`));
							return;
						}

						file = fs.createWriteStream(targetPath);
						let downloadedBytes = 0;

						redirectResponse.on('data', (chunk) => {
							downloadedBytes += chunk.length;
							if (downloadedBytes % (1024 * 1024) === 0) {
								log(`Downloaded ${downloadedBytes / 1024 / 1024} MB...`);
							}
						});

						redirectResponse.pipe(file);

						file.on('finish', () => {
							if (file) file.close();

							fs.stat(targetPath, (err, stats) => {
								if (err) {
									log(`Error checking file size: ${err.message}`);
									reject(err);
									return;
								}

								if (stats.size === 0) {
									log('Error: Downloaded file is empty (0 bytes)');
									fs.unlink(targetPath, () => {});
									reject(new Error('Downloaded file is empty'));
									return;
								}

								log(`Downloaded file size: ${stats.size} bytes`);

								fs.chmod(targetPath, 0o755, (err) => {
									if (err) {
										log(`Error making file executable: ${err.message}`);
										reject(err);
										return;
									}
									log('Download completed and file made executable');
									resolve();
								});
							});
						});

						file.on('error', (err) => {
							if (file) file.close();
							fs.unlink(targetPath, () => {}); // Delete the file on error
							log(`Error downloading file from redirect: ${err.message}`);
							reject(err);
						});
					}).on('error', (err) => {
						fs.unlink(targetPath, () => {}); // Delete the file on error
						log(`Error following redirect: ${err.message}`);
						reject(err);
					});

					return;
				}

				if (response.statusCode !== 200) {
					reject(new Error(`Failed to download: ${response.statusCode} ${response.statusMessage}`));
					return;
				}

				file = fs.createWriteStream(targetPath);
				let downloadedBytes = 0;

				response.on('data', (chunk) => {
					downloadedBytes += chunk.length;
					if (downloadedBytes % (1024 * 1024) === 0) {
						log(`Downloaded ${downloadedBytes / 1024 / 1024} MB...`);
					}
				});

				response.pipe(file);

				file.on('finish', () => {
					if (file) file.close();

					fs.stat(targetPath, (err, stats) => {
						if (err) {
							log(`Error checking file size: ${err.message}`);
							reject(err);
							return;
						}

						if (stats.size === 0) {
							log('Error: Downloaded file is empty (0 bytes)');
							fs.unlink(targetPath, () => {});
							reject(new Error('Downloaded file is empty'));
							return;
						}

						log(`Downloaded file size: ${stats.size} bytes`);

						fs.chmod(targetPath, 0o755, (err) => {
							if (err) {
								log(`Error making file executable: ${err.message}`);
								reject(err);
								return;
							}
							log('Download completed and file made executable');
							resolve();
						});
					});
				});

				file.on('error', (err) => {
					if (file) file.close();
					fs.unlink(targetPath, () => {}); // Delete the file on error
					log(`Error downloading file: ${err.message}`);
					reject(err);
				});
			}).on('error', (err) => {
				if (file) file.close();
				fs.unlink(targetPath, () => {}); // Delete the file on error
				log(`Error downloading file: ${err.message}`);
				reject(err);
			});

			request.setTimeout(30000, () => {
				request.destroy();
				if (file) file.close();
				fs.unlink(targetPath, () => {});
				reject(new Error('Download timed out after 30 seconds'));
			});
		});
	}

	log(`Server path: ${resolvedServerPath}`);
	log(`Cap'n Proto linked version: ${capnpVersion}`);
	log(`Import paths: ${resolvedImportPaths.join(', ')}`);

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

	const clientOptions: LanguageClientOptions = {
		documentSelector: [{ scheme: 'file', language: 'capnp' }],
		synchronize: {
			fileEvents: workspace.createFileSystemWatcher('**/*.capnp')
		},
		outputChannel: outputChannel,
		workspaceFolder: workspaceFolder,
		initializationOptions: {
			capnp: {
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
