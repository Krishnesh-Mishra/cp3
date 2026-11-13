// Diagnostics / error recovery while typing
import * as assert from "node:assert/strict";
import * as vscode from "vscode";
import { cleanupFixtures, diagnosticCases, writeWorkspaceCase } from "../../harness";

diagnosticCases("Diagnostics: error recovery fixtures", [
    {
        name: "a partially typed expression still surfaces an error",
        source: "const value = 1 +\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an empty call target is an error",
        source: "const r = ();\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a lone member access chain still surfaces an error",
        source: "import console from \"std/console\";\nconsole.\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an unterminated generic list surfaces an error",
        source: "class Box<T> {\n  value: T = 0 as T;\n\nconst b = new Box();\n",
        expect: [{ severity: "error" }],
    },
]);

suite("Diagnostics: error recovery while typing", () => {
    suiteTeardown(() => cleanupFixtures());

    // These cases need two passes over the same file: type a broken document,
    // confirm the error, then fix it and expect the diagnostics to clear.
    const fixCases = [
        {
            name: "fixing an unterminated string clears the diagnostic",
            broken: "const broken = \"never closed\n",
            fixed: "const broken = \"now closed\";\n",
        },
        {
            name: "fixing a dangling operator clears the diagnostic",
            broken: "const value = 1 +\n",
            fixed: "const value = 1 + 2;\n",
        },
        {
            name: "fixing a const rebinding clears the diagnostic",
            broken: "const stable = 1;\nstable = 2;\n",
            fixed: "const stable = 1;\nconst after = stable + 1;\n",
        },
        {
            name: "fixing an unknown member clears the diagnostic",
            broken: "const o = { a: 1 };\nconst b = o.b\n",
            fixed: "const o = { a: 1, b: 2 };\nconst b = o.b;\n",
        },
        {
            name: "fixing a type mismatch clears the diagnostic",
            broken: "let x: number = 1;\nx = \"bad\";\n",
            fixed: "let x: number = 1;\nx = 2;\n",
        },
        {
            name: "fixing a missing import clears the diagnostic",
            broken: "let x = Math.random();\n",
            fixed: "import Math from \"std/math\";\nlet x = Math.random();\n",
        },
        {
            name: "fixing a duplicate declaration clears the diagnostic",
            broken: "const value = 1;\nconst value = 2;\n",
            fixed: "const value = 1;\nconst value2 = 2;\n",
        },
        {
            name: "fixing a bad call argument clears the diagnostic",
            broken: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1);\n",
            fixed: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, 2);\n",
        },
    ];

    for (const fixCase of fixCases) {
        test(fixCase.name, async () => {
            const uri = writeWorkspaceCase(`recovery-${Math.random().toString(36).slice(2, 10)}.cp3`, fixCase.broken, "diagnostics");
            const document = await vscode.workspace.openTextDocument(uri);
            const deadline = Date.now() + 30_000;
            let broken = vscode.languages.getDiagnostics(document.uri);
            while (broken.length === 0 && Date.now() < deadline) {
                await new Promise((resolve) => setTimeout(resolve, 400));
                broken = vscode.languages.getDiagnostics(document.uri);
            }
            assert.ok(broken.length > 0, "expected at least one diagnostic on the broken source");
            const editor = await vscode.window.showTextDocument(document);
            const wholeRange = new vscode.Range(
                document.positionAt(0),
                document.positionAt(document.getText().length),
            );
            await editor.edit((builder) => builder.replace(wholeRange, fixCase.fixed));
            const fixedDeadline = Date.now() + 30_000;
            let fixed = vscode.languages.getDiagnostics(document.uri);
            while (fixed.some((d) => d.severity === vscode.DiagnosticSeverity.Error) && Date.now() < fixedDeadline) {
                await new Promise((resolve) => setTimeout(resolve, 400));
                fixed = vscode.languages.getDiagnostics(document.uri);
            }
            assert.ok(
                fixed.every((d) => d.severity !== vscode.DiagnosticSeverity.Error),
                `expected no errors after the fix; got: ${fixed.map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${String(d.code ?? "")} ${d.message}`).join(" | ")}`,
            );
        });
    }
});
