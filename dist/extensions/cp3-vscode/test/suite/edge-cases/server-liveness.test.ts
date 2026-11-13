// Regression: language server must start from a cold activation.
// The extension activates lazily on `onLanguage:cp3` - the document-open
// event that triggers activation fires BEFORE `activate()` registers the
// lazy-start handler, which previously left the server permanently
// unstarted. This case opens a .cp3 document in a fresh workspace and
// requires the server to answer (diagnostics and hover) without any manual
// restart.
import * as assert from "node:assert/strict";
import * as vscode from "vscode";
import { ensureCompilerConfigured, openWorkspaceCase, waitForCompletions } from "../harness";

suite("Language server: cold-activation regression", () => {
    test("a fresh workspace activates and the server answers diagnostics", async function () {
        this.timeout(60_000);
        await ensureCompilerConfigured();
        const document = await openWorkspaceCase(
            "cold-activation-liveness.cp3",
            'let x: number = 1;\nx = "bad";\n',
            "regression",
        );
        const deadline = Date.now() + 45_000;
        let diagnostics = vscode.languages.getDiagnostics(document.uri);
        while (diagnostics.length === 0 && Date.now() < deadline) {
            await new Promise((resolve) => setTimeout(resolve, 400));
            diagnostics = vscode.languages.getDiagnostics(document.uri);
        }
        assert.ok(diagnostics.length > 0, "the language server must publish diagnostics for a broken fixture");
    });

    test("a fresh workspace activates and the server answers completions", async function () {
        this.timeout(60_000);
        await ensureCompilerConfigured();
        const document = await openWorkspaceCase(
            "cold-activation-completion.cp3",
            'const user = {\n    name: "Test",\n    age: 25,\n};\n\nuser.\n',
            "regression",
        );
        const line = document.lineAt(5).text;
        assert.equal(line, "user.");
        const position = new vscode.Position(5, line.length);
        const labels = await waitForCompletions(document, position, ["name", "age"], 30_000);
        assert.ok(labels.includes("name"), `expected "name" in completions; got: ${labels.join(", ")}`);
        assert.ok(labels.includes("age"), `expected "age" in completions; got: ${labels.join(", ")}`);
    });
});
