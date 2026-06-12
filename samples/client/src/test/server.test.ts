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

        const config = vscode.workspace.getConfiguration('capnp-ls-client');
        console.log('Configuration:', {
            serverPath: config.get('languageServer.path'),
            capnpChannel: config.get('languageServer.capnpChannel')
        });

        assert.ok(config.get('languageServer.path'), 'Language server path is not configured');
        assert.ok(config.get('languageServer.capnpChannel'), 'Cap\'n Proto channel is not configured');
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

    test('Open Syntax Fixture', async () => {
        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/syntax.capnp'));
        const syntaxDocument = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(syntaxDocument);

        assert.strictEqual(syntaxDocument.languageId, 'capnp');
        assert.ok(syntaxDocument.getText().includes('$Cxx.namespace'));
    });

    test('Line Comment Configuration', async () => {
        const document = await vscode.workspace.openTextDocument({
            language: 'capnp',
            content: '@0xdba53d6c0e9fe301;\nstruct CommentTarget {}\n'
        });
        const editor = await vscode.window.showTextDocument(document);
        editor.selection = new vscode.Selection(1, 0, 1, 0);

        await vscode.commands.executeCommand('editor.action.commentLine');

        assert.strictEqual(document.lineAt(1).text, '# struct CommentTarget {}');
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

        // line 15, column 29 (0-indexed)
        const position = new vscode.Position(14, 28);
        console.log('Testing position:', position);

        const definitions = await vscode.commands.executeCommand<vscode.Location[]>(
            'vscode.executeDefinitionProvider',
            document.uri,
            position
        );

        console.log('Definitions found:', definitions?.length);
        assert.ok(definitions?.length > 0, 'No definitions found');
        assert.strictEqual(path.basename(definitions[0].uri.fsPath), 'company.capnp');
        assert.strictEqual(definitions[0].range.start.line, 17, 'Definition should be at line 18 (0-indexed)');
        assert.strictEqual(definitions[0].range.start.character, 2, 'Definition should start at character 3');
    });

    test('Hover Provider', async () => {
        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/company.capnp'));
        document = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(document);

        await new Promise(resolve => setTimeout(resolve, 2000));

        const hovers = await vscode.commands.executeCommand<vscode.Hover[]>(
            'vscode.executeHoverProvider',
            document.uri,
            new vscode.Position(14, 28)
        );

        assert.ok(hovers?.length > 0, 'No hover found');
        const markdown = hovers[0].contents
            .map(content => typeof content === 'string' ? content : content.value)
            .join('\n');
        assert.ok(markdown.includes('struct Employee'), 'Hover should include symbol metadata');
    });

    test('Document Symbol Provider', async () => {
        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/company.capnp'));
        document = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(document);

        await new Promise(resolve => setTimeout(resolve, 2000));

        const symbols = await vscode.commands.executeCommand<vscode.DocumentSymbol[]>(
            'vscode.executeDocumentSymbolProvider',
            document.uri
        );

        assert.ok(symbols?.length > 0, 'No document symbols found');
        assert.ok(
            symbols.some(symbol => symbol.name === 'EmployeeManagement'),
            'Document symbols should include the interface'
        );
        assert.ok(
            symbols.some(symbol => symbol.name === 'Employee'),
            'Document symbols should include the nested struct'
        );
        assert.ok(
            symbols.some(symbol => symbol.name === 'addEmployee'),
            'Document symbols should include methods'
        );
    });

    test('Reference Provider', async () => {
        const workspaceFolder = vscode.workspace.workspaceFolders![0];
        const uri = vscode.Uri.file(path.join(workspaceFolder.uri.fsPath, 'schemas/company.capnp'));
        document = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(document);

        await new Promise(resolve => setTimeout(resolve, 2000));

        const references = await vscode.commands.executeCommand<vscode.Location[]>(
            'vscode.executeReferenceProvider',
            document.uri,
            new vscode.Position(14, 28)
        );

        assert.ok(references?.length > 1, 'References should include declaration and usages');
        assert.ok(
            references.some(reference => reference.range.start.line === 17),
            'References should include the declaration'
        );
    });
});
