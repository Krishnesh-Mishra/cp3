// extensions/cp3-vscode/src/packageJsonSchema.ts
//
// A cp3 project's package.json is NOT the npm one: `typeSystem` selects the
// type-name vocabulary, `main` is a .cp3 entry and `scripts` invoke cp3c.
// VS Code's built-in JSON language server associates package.json with the
// npm schema unless the workspace overrides it, so this extension records
// the cp3 schema (schemas/cp3-package.schema.json) in the workspace
// `json.schemas` setting whenever the workspace root manifest is a cp3
// project. Workspace associations win over contributed ones, so npm
// projects are untouched: the setting is only written for detected cp3
// manifests and only ever replaces our own previous entry.

import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

export const SCHEMA_FILE = "cp3-package.schema.json";
export const APPLY_COMMAND = "cp3.applyPackageJsonSchema";

interface JsonSchemaAssoc {
    fileMatch?: string[];
    url?: string;
    schema?: string;
    [key: string]: unknown;
}

/** Parse tolerantly: a broken manifest is simply not a cp3 project. */
function readJsonFile(file: string): Record<string, unknown> | undefined {
    try {
        const text = fs.readFileSync(file, "utf8");
        const parsed: unknown = JSON.parse(text);
        if (parsed !== null && typeof parsed === "object" && !Array.isArray(parsed)) {
            return parsed as Record<string, unknown>;
        }
    } catch {
        // Missing or malformed manifest: not detected as cp3.
    }
    return undefined;
}

/** Strong cp3 signals: a .cp3 entry point, cp3c scripts, or typeSystem. */
function isCp3Manifest(manifest: Record<string, unknown>): boolean {
    const main = manifest["main"];
    if (typeof main === "string" && main.endsWith(".cp3")) {
        return true;
    }
    const typeSystem = manifest["typeSystem"];
    if (typeSystem === "typescript" || typeSystem === "rust" || typeSystem === "cpp") {
        return true;
    }
    const scripts = manifest["scripts"];
    if (scripts !== null && typeof scripts === "object" && !Array.isArray(scripts)) {
        const forRecord = scripts as Record<string, unknown>;
        for (const value of Object.values(forRecord)) {
            if (typeof value === "string" && /\bcp3c\b/.test(value)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Whether any workspace folder's root package.json is a cp3 project. Also
 * reused as the language-server start gate: an npm-only workspace should
 * not spawn cp3c just because a JSON file was opened.
 */
export async function workspaceLooksLikeCp3(): Promise<boolean> {
    for (const folder of vscode.workspace.workspaceFolders ?? []) {
        const manifest = readJsonFile(path.join(folder.uri.fsPath, "package.json"));
        if (manifest !== undefined && isCp3Manifest(manifest)) {
            return true;
        }
        // A loose .cp3 file in the workspace is a cp3 workspace too (a
        // project may predate its package.json); findFiles is bounded.
        const cp3Files = await vscode.workspace.findFiles("**/*.cp3", "**/node_modules/**", 1);
        if (cp3Files.length > 0) {
            return true;
        }
    }
    return false;
}

/** True when the value is one of our own earlier associations. */
function isOurs(assoc: JsonSchemaAssoc): boolean {
    const url = assoc.url ?? assoc.schema;
    return typeof url === "string" && url.includes(SCHEMA_FILE);
}

/**
 * Records the bundled cp3 schema in the workspace `json.schemas` setting
 * for `package.json`, once per workspace, replacing only our own entry.
 * Returns true when the workspace is a cp3 project.
 */
export async function applyPackageJsonSchema(
    context: vscode.ExtensionContext,
    announce: boolean
): Promise<boolean> {
    const isCp3 = await workspaceLooksLikeCp3();
    const schemaUri = vscode.Uri.joinPath(
        context.extensionUri,
        "schemas",
        SCHEMA_FILE
    ).toString();
    const config = vscode.workspace.getConfiguration("json");
    const current = config.get<JsonSchemaAssoc[]>("schemas", []) ?? [];
    // Drop our own earlier entry; only re-add it for a cp3 project, so an
    // npm workspace keeps exactly the npm schema and a cp3 project never
    // accumulates duplicates.
    const kept = current.filter((assoc) => !isOurs(assoc));
    const next: JsonSchemaAssoc[] = isCp3
        ? [...kept, { fileMatch: ["package.json"], url: schemaUri }]
        : kept;
    if (JSON.stringify(next) !== JSON.stringify(current)) {
        await config.update("schemas", next, vscode.ConfigurationTarget.Workspace);
    }
    if (announce) {
        void vscode.window.showInformationMessage(
            isCp3
                ? "cp3: package.json now uses the cp3 manifest schema."
                : "cp3: no cp3 project (package.json) found in this workspace; the default JSON schema applies."
        );
    }
    return isCp3;
}

/** The `cp3.applyPackageJsonSchema` command. */
export function registerPackageJsonSchemaCommand(
    context: vscode.ExtensionContext
): void {
    context.subscriptions.push(
        vscode.commands.registerCommand(APPLY_COMMAND, () =>
            applyPackageJsonSchema(context, true)
        )
    );
}
