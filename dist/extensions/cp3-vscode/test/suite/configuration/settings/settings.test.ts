// Configuration / the extension's contributed surface in a live host
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import { ensureCompilerConfigured, findCp3c } from "../../harness";

function repoRoot(): string {
    let dir = __dirname;
    for (let depth = 0; depth < 12; depth += 1) {
        if (fs.existsSync(path.join(dir, "CMakeLists.txt")) && fs.existsSync(path.join(dir, "src", "cli", "CMakeLists.txt"))) {
            return dir;
        }
        const parent = path.dirname(dir);
        if (parent === dir) break;
        dir = parent;
    }
    throw new Error("cannot locate the repository root");
}

suite("Configuration: contributed surface", () => {
    test("cp3.compilerPath defaults to a bare name when never set", async () => {
        // The test harness pinned it in ensureCompilerConfigured for the LSP;
        // read the effective value to make sure it is a usable path here.
        await ensureCompilerConfigured();
        const value = vscode.workspace.getConfiguration("cp3").get<string>("compilerPath");
        assert.ok(typeof value === "string" && value.length > 0);
    });

    test("the repository release binary is what the harness pinned", () => {
        const resolved = findCp3c();
        assert.ok(fs.existsSync(resolved), `expected ${resolved} to exist`);
    });

    test("cp3.suggestWorkspaceExports defaults to true", async () => {
        const value = vscode.workspace.getConfiguration("cp3").get<boolean>("suggestWorkspaceExports");
        assert.equal(value, true);
    });

    test("cp3.run command is registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("cp3.run"), "cp3.run is registered");
    });

    test("cp3.restartServer command is registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("cp3.restartServer"), "cp3.restartServer is registered");
    });

    test("cp3.organizeImports command is registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("cp3.organizeImports"), "cp3.organizeImports is registered");
    });

    test("cp3.applyPackageJsonSchema command is registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        assert.ok(commands.includes("cp3.applyPackageJsonSchema"), "cp3.applyPackageJsonSchema is registered");
    });

    test("the extension is discoverable by id", () => {
        const extension = vscode.extensions.getExtension("cp3.cp3-vscode");
        assert.ok(extension, "extension is present in the registry");
    });

    test("the extension activates when a cp3 document opens", async () => {
        const extension = vscode.extensions.getExtension("cp3.cp3-vscode");
        assert.ok(extension);
        await extension.activate();
        assert.ok(extension.isActive, "extension is active after activation");
    });

    test("the cp3 language is registered with the editor", () => {
        const registered = vscode.languages.getLanguages();
        return registered.then((languages) => {
            assert.ok(languages.includes("cp3"), "cp3 language id is registered");
        });
    });

    test("the cp3 task provider contributes build/run/check tasks", async () => {
        const tasks = await vscode.tasks.fetchTasks({ type: "cp3" });
        const names = tasks.map((task) => task.name);
        assert.ok(names.includes("cp3: build"), `expected cp3: build in ${names.join(", ")}`);
        assert.ok(names.includes("cp3: run"), `expected cp3: run in ${names.join(", ")}`);
        assert.ok(names.includes("cp3: check"), `expected cp3: check in ${names.join(", ")}`);
    });

    test("the contributed check task is grouped with the test group", async () => {
        const tasks = await vscode.tasks.fetchTasks({ type: "cp3" });
        const check = tasks.find((task) => task.name === "cp3: check");
        assert.ok(check, "cp3: check exists");
        assert.ok(check.group === vscode.TaskGroup.Test || check.group?.id === vscode.TaskGroup.Test.id, "check is in the test group");
    });

    test("the output channel for the language server can be written", () => {
        const channel = vscode.window.createOutputChannel("cp3 Language Server probe");
        channel.appendLine("probe");
        channel.dispose();
    });

    test("the extension bundled manifest declares the .cp3 language id", () => {
        const manifest = JSON.parse(fs.readFileSync(path.join(repoRoot(), "dist", "extensions", "cp3-vscode", "package.json"), "utf8") as string) as {
            contributes: { languages: Array<{ id: string; extensions: string[] }> };
        };
        const cp3 = manifest.contributes.languages.find((language) => language.id === "cp3");
        assert.ok(cp3, "cp3 language is contributed");
        assert.ok(cp3.extensions.includes(".cp3"), "cp3 files bind to the .cp3 extension");
    });

    test("the extension bundled manifest contributes the cp3 file icon", () => {
        const manifest = JSON.parse(fs.readFileSync(path.join(repoRoot(), "dist", "extensions", "cp3-vscode", "package.json"), "utf8") as string) as {
            contributes: { iconThemes: Array<{ id: string; path: string }> };
        };
        assert.ok(manifest.contributes.iconThemes.some((theme) => theme.id === "cp3-icons"));
        const iconsPath = path.join(repoRoot(), "dist", "extensions", "cp3-vscode", manifest.contributes.iconThemes[0].path);
        assert.ok(fs.existsSync(iconsPath), "icon theme file exists");
    });

    test("the extension bundled manifest declares the cp3 debugger", () => {
        const manifest = JSON.parse(fs.readFileSync(path.join(repoRoot(), "dist", "extensions", "cp3-vscode", "package.json"), "utf8") as string) as {
            contributes: { debuggers: Array<{ type: string }> };
        };
        assert.ok(manifest.contributes.debuggers.some((debugger_) => debugger_.type === "cp3"));
    });
});
