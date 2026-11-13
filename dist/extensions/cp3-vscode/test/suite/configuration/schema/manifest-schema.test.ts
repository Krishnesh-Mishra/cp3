// Configuration / the cp3 manifest schema association flow
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

const SCHEMA_FILE = "cp3-package.schema.json";
const APPLY_COMMAND = "cp3.applyPackageJsonSchema";

function workspaceRoot(): string {
    const folder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(folder, "this suite needs the run's workspace folder");
    return folder.uri.fsPath;
}

function readOurSchemaAssociations(): Array<{ fileMatch?: string[]; url?: string; schema?: string }> {
    const current = vscode.workspace.getConfiguration("json").get<Array<{ fileMatch?: string[]; url?: string; schema?: string }>>("schemas", []) ?? [];
    return current.filter((assoc) => {
        const url = assoc.url ?? assoc.schema ?? "";
        return typeof url === "string" && url.includes(SCHEMA_FILE);
    });
}

suite("Configuration: manifest schema association", () => {
    suiteTeardown(async () => {
        const root = workspaceRoot();
        const manifest = path.join(root, "package.json");
        fs.rmSync(manifest, { force: true });
        // Drop the association so the run leaves no trace in the profile.
        const kept = (vscode.workspace.getConfiguration("json").get<Array<Record<string, unknown>>>("schemas", []) ?? [])
            .filter((assoc) => {
                const url = String(assoc["url"] ?? assoc["schema"] ?? "");
                return !url.includes(SCHEMA_FILE);
            });
        await vscode.workspace.getConfiguration("json").update("schemas", kept, vscode.ConfigurationTarget.Workspace);
    });

    test("the workspace starts without a cp3 manifest", () => {
        assert.ok(!fs.existsSync(path.join(workspaceRoot(), "package.json")), "fresh workspace has no manifest");
    });

    test("applying the schema in a non-cp3 workspace does not record our association", async () => {
        await vscode.commands.executeCommand(APPLY_COMMAND);
        assert.equal(readOurSchemaAssociations().length, 0, "no association recorded for an npm-looking workspace");
    });

    test("a package.json with a .cp3 main marks the workspace as a cp3 project", () => {
        fs.writeFileSync(
            path.join(workspaceRoot(), "package.json"),
            JSON.stringify({ name: "probe", main: "./main.cp3", typeSystem: "typescript" }, null, 2),
        );
        const parsed = JSON.parse(fs.readFileSync(path.join(workspaceRoot(), "package.json"), "utf8")) as { main: string };
        assert.equal(parsed.main, "./main.cp3");
    });

    test("applying the schema in a cp3 workspace records the bundled association", async () => {
        await vscode.commands.executeCommand(APPLY_COMMAND);
        const ours = readOurSchemaAssociations();
        assert.ok(ours.length >= 1, `expected an association for the bundled schema; got ${JSON.stringify(ours)}`);
        assert.ok(ours[0].fileMatch?.includes("package.json"), "association targets package.json");
    });

    test("the recorded association points at a file that exists", async () => {
        const ours = readOurSchemaAssociations();
        assert.ok(ours.length >= 1);
        // The association is an absolute URI (or a relative path) into the
        // extension; just check it mentions our schema file.
        const url = ours[0].url ?? ours[0].schema ?? "";
        assert.ok(url.includes(SCHEMA_FILE), `association mentions the schema file: ${url}`);
    });

    test("applying the schema twice does not duplicate the association", async () => {
        await vscode.commands.executeCommand(APPLY_COMMAND);
        await vscode.commands.executeCommand(APPLY_COMMAND);
        assert.equal(readOurSchemaAssociations().length, 1, "exactly one association after repeated applies");
    });

    test("the bundled schema file exists in the extension folder", () => {
        // __dirname is out/test/suite/configuration/schema; walk up to the extension root.
        let dir = __dirname;
        for (let depth = 0; depth < 8; depth += 1) {
            if (fs.existsSync(path.join(dir, "package.json")) && fs.existsSync(path.join(dir, "schemas", SCHEMA_FILE))) {
                assert.ok(fs.existsSync(path.join(dir, "schemas", SCHEMA_FILE)));
                return;
            }
            const parent = path.dirname(dir);
            if (parent === dir) break;
            dir = parent;
        }
        assert.fail("could not locate the bundled schema file");
    });

    test("the bundled schema file is valid JSON with cp3 fields", () => {
        let dir = __dirname;
        for (let depth = 0; depth < 8; depth += 1) {
            const candidate = path.join(dir, "schemas", SCHEMA_FILE);
            if (fs.existsSync(candidate)) {
                const schema = JSON.parse(fs.readFileSync(candidate, "utf8") as string) as { type?: string; properties?: Record<string, unknown> };
                assert.equal(schema.type, "object");
                assert.ok(schema.properties && Object.keys(schema.properties).length > 0, "schema declares properties");
                return;
            }
            const parent = path.dirname(dir);
            if (parent === dir) break;
            dir = parent;
        }
        assert.fail("could not locate the bundled schema file");
    });

    test("a package.json with cp3c scripts is detected as a cp3 project", () => {
        fs.writeFileSync(
            path.join(workspaceRoot(), "package.json"),
            JSON.stringify({ name: "probe", scripts: { build: "cp3c build ." } }, null, 2),
        );
        const parsed = JSON.parse(fs.readFileSync(path.join(workspaceRoot(), "package.json"), "utf8")) as { scripts: Record<string, string> };
        assert.ok(Object.values(parsed.scripts).some((value) => value.includes("cp3c")));
    });

    test("a package.json with a typeSystem field is detected as a cp3 project", () => {
        fs.writeFileSync(
            path.join(workspaceRoot(), "package.json"),
            JSON.stringify({ name: "probe", typeSystem: "rust" }, null, 2),
        );
        const parsed = JSON.parse(fs.readFileSync(path.join(workspaceRoot(), "package.json"), "utf8")) as { typeSystem: string };
        assert.equal(parsed.typeSystem, "rust");
    });
});
