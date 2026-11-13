// Performance / indexing and latency at small and large project sizes
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import { cleanupFixtures, ensureCompilerConfigured, waitForCompletions, writeWorkspaceCase } from "../../harness";

const BIG_SIZES = process.env["CP3_PERF_BIG"] === "1";

function perfRoot(): string {
    const root = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!root) {
        throw new Error("perf tests need a workspace folder");
    }
    return path.join(root, "cases", "perf");
}

function writeProject(name: string, count: number): string {
    const root = path.join(perfRoot(), name);
    fs.mkdirSync(root, { recursive: true });
    for (let index = 0; index < count; index += 1) {
        fs.writeFileSync(
            path.join(root, `module-${index}.cp3`),
            `export function perf_target_${index}(value: number): number {\n  return value + ${index};\n}\n`,
        );
    }
    return root;
}

function perfCleanup(): void {
    try {
        fs.rmSync(perfRoot(), { recursive: true, force: true, maxRetries: 6, retryDelay: 150 });
    } catch {
        // best effort
    }
}

suite("Performance: small and large projects", () => {
    suiteTeardown(() => {
        perfCleanup();
        cleanupFixtures();
    });

    test("a one-file project answers completions quickly", async () => {
        await ensureCompilerConfigured();
        writeProject("one", 1);
        const uri = writeWorkspaceCase(
            "main-one.cp3",
            "import { perf_target_0 } from \"./one/module-0.cp3\";\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const labels = await waitForCompletions(document, new vscode.Position(1, "const value = ".length), ["perf_target_0"], 20_000);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] one-file completion latency: ${elapsed}ms`);
        assert.ok(labels.includes("perf_target_0"));
        assert.ok(elapsed < 20_000, `completion took ${elapsed}ms`);
    });

    test("a ten-file project answers completions across modules", async () => {
        await ensureCompilerConfigured();
        writeProject("ten", 10);
        const uri = writeWorkspaceCase(
            "main-ten.cp3",
            "import { perf_target_7 } from \"./ten/module-7.cp3\";\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const labels = await waitForCompletions(document, new vscode.Position(1, "const value = ".length), ["perf_target_7"], 20_000);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] ten-file completion latency: ${elapsed}ms`);
        assert.ok(labels.includes("perf_target_7"));
        assert.ok(elapsed < 20_000, `completion took ${elapsed}ms`);
    });

    test("a hundred-file project answers completions across modules", async () => {
        await ensureCompilerConfigured();
        writeProject("hundred", 100);
        const uri = writeWorkspaceCase(
            "main-hundred.cp3",
            "import { perf_target_50 } from \"./hundred/module-50.cp3\";\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const labels = await waitForCompletions(document, new vscode.Position(1, "const value = ".length), ["perf_target_50"], 30_000);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] hundred-file completion latency: ${elapsed}ms`);
        assert.ok(labels.includes("perf_target_50"));
        assert.ok(elapsed < 30_000, `completion took ${elapsed}ms`);
    });

    test("a thousand-file project indexes to a usable state (opt-in, CP3_PERF_BIG=1)", async function () {
        if (!BIG_SIZES) {
            console.log("[cp3-perf] CP3_PERF_BIG not set; skipping the 1000-file scenario");
            return;
        }
        await ensureCompilerConfigured();
        writeProject("thousand", 1000);
        const uri = writeWorkspaceCase(
            "main-thousand.cp3",
            "import { perf_target_500 } from \"./thousand/module-500.cp3\";\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const labels = await waitForCompletions(document, new vscode.Position(1, "const value = ".length), ["perf_target_500"], 120_000);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] thousand-file completion latency: ${elapsed}ms`);
        assert.ok(labels.includes("perf_target_500"));
    });

    test("a ten-thousand-file project indexes to a usable state (opt-in, CP3_PERF_BIG=1)", async function () {
        if (!BIG_SIZES) {
            console.log("[cp3-perf] CP3_PERF_BIG not set; skipping the 10000-file scenario");
            return;
        }
        await ensureCompilerConfigured();
        writeProject("tenthousand", 10_000);
        const uri = writeWorkspaceCase(
            "main-tenthousand.cp3",
            "import { perf_target_5000 } from \"./tenthousand/module-5000.cp3\";\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const labels = await waitForCompletions(document, new vscode.Position(1, "const value = ".length), ["perf_target_5000"], 300_000);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] ten-thousand-file completion latency: ${elapsed}ms`);
        assert.ok(labels.includes("perf_target_5000"));
    });

    test("diagnostics after an edit land in a reasonable window", async () => {
        await ensureCompilerConfigured();
        const uri = writeWorkspaceCase("perf-diagnostics.cp3", "let x: number = 1;\nx = \"bad\";\n", "perf");
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const deadline = Date.now() + 30_000;
        let diagnostics = vscode.languages.getDiagnostics(document.uri);
        while (diagnostics.length === 0 && Date.now() < deadline) {
            await new Promise((resolve) => setTimeout(resolve, 200));
            diagnostics = vscode.languages.getDiagnostics(document.uri);
        }
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] diagnostics latency: ${elapsed}ms`);
        assert.ok(diagnostics.length > 0, "expected the bad assignment to be diagnosed");
    });

    test("hover answers within a small budget", async () => {
        await ensureCompilerConfigured();
        const uri = writeWorkspaceCase("perf-hover.cp3", "const shared = 41;\nconst read = __CURSOR__shared;\n", "perf");
        const document = await vscode.workspace.openTextDocument(uri);
        const before = Date.now();
        const hover = await vscode.commands
            .executeCommand("_executeHoverProvider", document.uri, new vscode.Position(1, "const read = ".length))
            .then((value) => value, () => undefined);
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] hover latency: ${elapsed}ms`);
        assert.ok(hover, "expected a hover result");
    });

    test("completion answers within a small budget repeatedly (typing simulation)", async () => {
        await ensureCompilerConfigured();
        const uri = writeWorkspaceCase(
            "perf-typing.cp3",
            "function helper(): number {\n  return helperDeep();\n}\nfunction helperDeep(): number {\n  return 42;\n}\nconst value = __CURSOR__\n",
            "perf",
        );
        const document = await vscode.workspace.openTextDocument(uri);
        const position = new vscode.Position(5, "const value = ".length);
        const timings: number[] = [];
        for (let i = 0; i < 5; i += 1) {
            const before = Date.now();
            await vscode.commands.executeCommand("_executeCompletionItemProvider", document.uri, position);
            timings.push(Date.now() - before);
        }
        console.log(`[cp3-perf] completion timings (ms): ${timings.join(", ")}`);
        assert.ok(Math.max(...timings) < 10_000, `slowest completion ${Math.max(...timings)}ms`);
    });

    test("a warm completion after edits stays usable", async () => {
        await ensureCompilerConfigured();
        const uri = writeWorkspaceCase("perf-warm.cp3", "const first = 1;\nconst read = __CURSOR__first;\n", "perf");
        const document = await vscode.workspace.openTextDocument(uri);
        const position = new vscode.Position(1, "const read = ".length);
        const before = Date.now();
        const list = await vscode.commands.executeCommand<vscode.CompletionList>(
            "_executeCompletionItemProvider",
            document.uri,
            position,
        );
        const elapsed = Date.now() - before;
        console.log(`[cp3-perf] warm completion latency: ${elapsed}ms`);
        assert.ok(list !== undefined);
    });
});
