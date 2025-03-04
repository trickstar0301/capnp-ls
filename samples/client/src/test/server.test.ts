// Copyright (c) 2024 Atsushi Tomida
// 
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

import * as vscode from 'vscode';
import * as assert from 'assert';
import * as path from 'path';

suite('Cap\'n Proto Language Server Test Suite', () => {
    let document: vscode.TextDocument;

    test('Initialize', async () => {
        console.log('Workspace folders:', vscode.workspace.workspaceFolders);
        
        if (!vscode.workspace.workspaceFolders) {
            console.log('No workspace folders found');
        } else {
            console.log('Found workspace folders:', vscode.workspace.workspaceFolders.map(f => f.uri.fsPath));
        }

        assert.ok(vscode.workspace.workspaceFolders?.length, 'No workspace is opened');

        await new Promise(resolve => setTimeout(resolve, 1000));

        const config = vscode.workspace.getConfiguration('capnproto');
        console.log('Configuration:', {
            serverPath: config.get('languageServer.path'),
            compilerPath: config.get('compiler.path'),
            importPaths: config.get('compiler.importPaths')
        });

        assert.ok(config.get('languageServer.path'), 'Language server path is not configured');
        assert.ok(config.get('compiler.path'), 'Compiler path is not configured');
        assert.ok(Array.isArray(config.get('compiler.importPaths')), 'Import paths are not configured');
    });

    test('Open Document', async () => {
        console.log('Starting Open Document test');
        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        console.log('Workspace folder:', workspaceFolder?.uri.fsPath);
        
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/company.capnp'));
        console.log('Document URI:', uri.fsPath);
        
        document = await vscode.workspace.openTextDocument(uri);
        console.log('Document opened:', document.uri.fsPath);
        
        await vscode.window.showTextDocument(document);
        assert.strictEqual(document.languageId, 'capnp');
    });

    test('Definition Provider', async () => {
        console.log('Starting Definition Provider test');

        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/company.capnp'));
        document = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(document);
        console.log('Document opened:', document.uri.fsPath);
        
        // TODO: check compile is done without timeout
        await new Promise(resolve => setTimeout(resolve, 2000));
        
        // line 13, column 31 (0-indexed)
        const position = new vscode.Position(12, 30);
        console.log('Testing position:', position);
        
        const definitions = await vscode.commands.executeCommand<vscode.Location[]>(
            'vscode.executeDefinitionProvider',
            document.uri,
            position
        );

        console.log('Definitions found:', definitions?.length);
        assert.ok(definitions?.length > 0, 'No definitions found');
        // assert.strictEqual(path.basename(definitions[0].uri.fsPath), 'company.capnp');
    });
}); 