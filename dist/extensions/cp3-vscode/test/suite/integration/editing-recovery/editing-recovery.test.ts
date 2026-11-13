// Integration / editing: diagnostics appear then clear as the file is fixed
import * as assert from "node:assert/strict";
import * as vscode from "vscode";
import { cleanupFixtures, ensureCompilerConfigured, writeWorkspaceCase } from "../../harness";

suite("Integration: editing + diagnostics recovery", () => {
    suiteTeardown(() => cleanupFixtures());

    const cases = [
        { name: "fixing a const rebinding clears its diagnostic", broken: "const stable = 1;\nstable = 2;\n", fixed: "const stable = 1;\nconst after = stable + 1;\n" },
        { name: "fixing a type mismatch clears its diagnostic", broken: "let x: number = 1;\nx = \"bad\";\n", fixed: "let x: number = 1;\nx = 2;\n" },
        { name: "fixing an unterminated string clears its diagnostic", broken: "const broken = \"\n", fixed: "const broken = \"closed\";\n" },
        { name: "fixing a missing import clears its diagnostic", broken: "let x = Math.random();\n", fixed: "import Math from \"std/math\";\nlet x = Math.random();\n" },
        { name: "fixing an unknown member clears its diagnostic", broken: "const o = { a: 1 };\nconst b = o.b;\n", fixed: "const o = { a: 1, b: 2 };\nconst b = o.b;\n" },
        { name: "fixing a call argument count clears its diagnostic", broken: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1);\n", fixed: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, 2);\n" },
        { name: "fixing an argument type clears its diagnostic", broken: "function bump(n: number): void {}\nbump(\"one\");\n", fixed: "function bump(n: number): void {}\nbump(1);\n" },
        { name: "fixing a duplicate declaration clears its diagnostic", broken: "const value = 1;\nconst value = 2;\n", fixed: "const value = 1;\nconst value2 = 2;\n" },
        { name: "fixing a bad interface implementation clears its diagnostic", broken: "interface Sized {\n  size: number;\n}\nclass Impl implements Sized {\n}\n", fixed: "interface Sized {\n  size: number;\n}\nclass Impl implements Sized {\n  size: number = 1;\n}\n" },
        { name: "fixing a bad throw of a number clears its diagnostic", broken: "try {\n  throw 42;\n} catch (e) {\n  print(\"caught\");\n}\n", fixed: "try {\n  throw new Error(\"x\");\n} catch (e) {\n  print(\"caught\");\n}\n" },
        { name: "fixing a cross-file undefined import clears its diagnostic", broken: "import { helper } from \"./missing.cp3\";\nconst value = helper();\n", fixed: "function helper(): number { return 1; }\nconst value = helper();\n" },
        { name: "fixing an object literal member type clears its diagnostic", broken: "const o = { a: 1, b: 2 };\no.a = \"text\";\n", fixed: "const o = { a: 1, b: 2 };\no.a = 3;\n" },
        { name: "fixing a bad generic instantiation clears its diagnostic", broken: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<number>();\nb.value = \"str\";\n", fixed: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<number>();\nb.value = 42;\n" },
        { name: "fixing a stray closing paren clears its diagnostic", broken: "const a = 1;\n);\n", fixed: "const a = 1;\nconst b = 2;\n" },
        { name: "fixing a bad event emitter import clears its diagnostic", broken: "import { EventEmitter } from \"std/events\";\nlet e = new EventEmitter();\n", fixed: "let e = new EventEmitter();\n" },
        { name: "fixing a named console import clears its diagnostic", broken: "import { console } from \"std/console\";\nconsole.log(\"x\");\n", fixed: "import console from \"std/console\";\nconsole.log(\"x\");\n" },
        { name: "fixing a bare Math without import clears its diagnostic", broken: "let x = Math.random();\n", fixed: "import Math from \"std/math\";\nlet x = Math.random();\n" },
        { name: "fixing a cannot-find-name clears its diagnostic", broken: "console.log(message);\nlet message = \"hi\";\n", fixed: "let message = \"hi\";\nconsole.log(message);\n" },
        { name: "fixing a bad arrow argument clears its diagnostic", broken: "function each(cb: (item: number) => void): void {}\neach((item: string) => {});\n", fixed: "function each(cb: (item: number) => void): void {}\neach((item: number) => {});\n" },
        { name: "fixing an unterminated template clears its diagnostic", broken: "const broken = `never closed\n", fixed: "const broken = `now closed`;\n" },
    ];

    for (const fixCase of cases) {
        test(fixCase.name, async function () {
            this.timeout(60_000);
            await ensureCompilerConfigured();
            const uri = writeWorkspaceCase(
                `recovery-${Math.random().toString(36).slice(2, 10)}.cp3`,
                fixCase.broken,
                "integration",
            );
            const document = await vscode.workspace.openTextDocument(uri);
            const errorDeadline = Date.now() + 30_000;
            let broken = vscode.languages.getDiagnostics(document.uri);
            while (broken.length === 0 && Date.now() < errorDeadline) {
                await new Promise((resolve) => setTimeout(resolve, 300));
                broken = vscode.languages.getDiagnostics(document.uri);
            }
            assert.ok(broken.length > 0, "expected at least one diagnostic on the broken source");
            const editor = await vscode.window.showTextDocument(document);
            const wholeRange = new vscode.Range(
                document.positionAt(0),
                document.positionAt(document.getText().length),
            );
            await editor.edit((builder) => builder.replace(wholeRange, fixCase.fixed));
            const clearedDeadline = Date.now() + 30_000;
            let fixed = vscode.languages.getDiagnostics(document.uri);
            while (fixed.some((d) => d.severity === vscode.DiagnosticSeverity.Error) && Date.now() < clearedDeadline) {
                await new Promise((resolve) => setTimeout(resolve, 300));
                fixed = vscode.languages.getDiagnostics(document.uri);
            }
            assert.ok(
                fixed.every((d) => d.severity !== vscode.DiagnosticSeverity.Error),
                `expected no errors after the fix; got: ${fixed
                    .map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${String(d.code ?? "")} ${d.message}`)
                    .join(" | ")}`,
            );
        });
    }
});
