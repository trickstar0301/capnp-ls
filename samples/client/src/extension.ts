// Copyright (c) 2024 Atsushi Tomida
// 
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as path from 'path';
import * as fs from 'fs';
import { workspace, ExtensionContext, window } from 'vscode';
import * as https from 'https';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient;

// Default language server version for linux x86_64
const DEFAULT_SERVER_VERSION = 'v0.0.1';

export async function activate(context: ExtensionContext) {
	if (process.platform === 'win32') {
		window.showWarningMessage('Windows is currently not supported by Cap\'n Proto Language Server. Some features may not work as expected.');
	}

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
	// Get server version from configuration or use default
	const serverVersion = config.get<string>('languageServer.version') || DEFAULT_SERVER_VERSION;

	// Resolve environment variables in paths
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

	const outputChannel = window.createOutputChannel('Cap\'n Proto LSP');

	function log(message: string): void {
		outputChannel.appendLine(`[Client] ${message}`);
	}

	let resolvedServerPath: string;
	
	if (serverPathRaw) {
		const serverPath = resolveEnvVars(serverPathRaw);
		resolvedServerPath = path.isAbsolute(serverPath) 
			? serverPath 
			: context.asAbsolutePath(serverPath);
	} else {
		resolvedServerPath = await findLanguageServer(context, serverVersion);
	}

	async function findLanguageServer(context: ExtensionContext, version: string): Promise<string> {
		const candidatePaths: string[] = [];
		
		const extensionPath = context.extensionPath;
		const binaryName = process.platform === 'win32' ? 'capnp-ls.exe' : 'capnp-ls';
		const extensionBinaryPath = path.join(extensionPath, binaryName);
		
		candidatePaths.push(extensionBinaryPath);
		
		if (process.env.PATH) {
			const pathDirs = process.env.PATH.split(path.delimiter);
			for (const dir of pathDirs) {
				candidatePaths.push(path.join(dir, binaryName));
			}
		}
		
		// Check if we need to download the binary for Linux x86_64
		const isLinuxX86_64 = process.platform === 'linux' && process.arch === 'x64';
		if (isLinuxX86_64 && !fs.existsSync(extensionBinaryPath)) {
			log(`Binary not found at ${extensionBinaryPath} and we're on Linux x86_64, attempting to download...`);
			try {
				await downloadCapnpLs(extensionBinaryPath, version);
				log(`Successfully downloaded capnp-ls version ${version} to ${extensionBinaryPath}`);
				return extensionBinaryPath;
			} catch (err) {
				log(`Failed to download capnp-ls: ${err.message}`);
				// Continue with normal search path if download fails
			}
		}
		
		for (const candidatePath of candidatePaths) {
			if (fs.existsSync(candidatePath)) {
				try {
					fs.accessSync(candidatePath, fs.constants.X_OK);
					log(`Found language server at: ${candidatePath}`);
					return candidatePath;
				} catch (e) {
					log(`Found language server at ${candidatePath} but it's not executable`);
					if (process.platform === 'win32') {
						return candidatePath;
					}
				}
			}
		}
		
		log('Could not find capnp-ls, falling back to "capnp-ls" command');
		window.showWarningMessage('Could not find capnp-ls executable. Make sure it is installed and in your PATH.');
		return 'capnp-ls';
	}

	function downloadCapnpLs(targetPath: string, version: string): Promise<void> {
		const url = `https://github.com/trickstar0301/capnp-ls/releases/download/${version}/capnp-ls-linux-x86_64`;
		log(`Downloading capnp-ls version ${version} from ${url} to ${targetPath}`);
		
		return new Promise((resolve, reject) => {
			// ファイルのオープンを遅延させる
			let file: fs.WriteStream | null = null;
			
			// GitHubはUser-Agentを要求することがある
			const options = {
				headers: {
					'User-Agent': 'VSCode-CapnProto-Extension',
					'Accept': 'application/octet-stream'
				},
				followRedirects: true // Node.jsの新しいバージョンではサポートされている
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
								
								// 実行可能にする
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
			
			// タイムアウトの設定
			request.setTimeout(30000, () => {
				request.destroy();
				if (file) file.close();
				fs.unlink(targetPath, () => {});
				reject(new Error('Download timed out after 30 seconds'));
			});
		});
	}

	log(`Server path: ${resolvedServerPath}`);
	log(`Compiler path: ${compilerPath}`);
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