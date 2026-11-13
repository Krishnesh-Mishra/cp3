import * as vscode from "vscode";

interface ExportedSymbol {
    readonly name: string;
    readonly kind: vscode.CompletionItemKind;
    readonly file: vscode.Uri;
    readonly detail: string;
    readonly documentation: string;
}

let exportIndex: ExportedSymbol[] = [];
let indexBuild: Promise<void> | undefined;
let rebuildTimer: ReturnType<typeof setTimeout> | undefined;

const exportPattern = /^\s*export\s+(?:(?:declare|default)\s+)*(?:(?:async|abstract)\s+)*(function|class|interface|enum|type|const|let|var)\s+([A-Za-z_$][\w$]*)/gm;
const exportListPattern = /^\s*export\s*\{([^}]+)\}(?:\s*from\s*["'][^"']+["'])?\s*;?/gm;
const importLinePattern = /^\s*import\s+.*;?\s*$/;

function kindFor(kind: string): vscode.CompletionItemKind {
    switch (kind) {
        case "function": return vscode.CompletionItemKind.Function;
        case "class": return vscode.CompletionItemKind.Class;
        case "interface": return vscode.CompletionItemKind.Interface;
        case "enum": return vscode.CompletionItemKind.Enum;
        case "type": return vscode.CompletionItemKind.Struct;
        default: return vscode.CompletionItemKind.Variable;
    }
}

function declarationDoc(text: string, declarationOffset: number): string {
    const before = text.slice(0, declarationOffset);
    const match = before.match(/\/\*\*[\s\S]*?\*\/\s*$/);
    return match?.[0].replace(/^\s*\/\*\*|\*\/\s*$/g, "")
        .split("\n").map((line) => line.replace(/^\s*\* ?/, "")).join("\n").trim() ?? "";
}

function parseExports(uri: vscode.Uri, text: string): ExportedSymbol[] {
    const entries: ExportedSymbol[] = [];
    exportPattern.lastIndex = 0;
    for (let match = exportPattern.exec(text); match; match = exportPattern.exec(text)) {
        const kind = match[1];
        const name = match[2];
        if (name === "default") continue;
        entries.push({
            name,
            kind: kindFor(kind),
            file: uri,
            detail: `exported ${kind} · ${vscode.workspace.asRelativePath(uri, false)}`,
            documentation: declarationDoc(text, match.index),
        });
    }
    exportListPattern.lastIndex = 0;
    for (let match = exportListPattern.exec(text); match; match = exportListPattern.exec(text)) {
        for (const specifier of match[1].split(",")) {
            const names = specifier.trim().split(/\s+as\s+/);
            const exported = names.length > 1 ? names[names.length - 1].trim() : names[0].trim();
            if (!/^[A-Za-z_$][\w$]*$/.test(exported) || exported === "default") continue;
            entries.push({
                name: exported,
                kind: vscode.CompletionItemKind.Variable,
                file: uri,
                detail: `named export · ${vscode.workspace.asRelativePath(uri, false)}`,
                documentation: "",
            });
        }
    }
    return entries;
}

async function rebuildExportIndex(): Promise<void> {
    if (indexBuild) return indexBuild;
    indexBuild = (async () => {
        const files = await vscode.workspace.findFiles(
            "**/*.cp3",
            "**/{build,dist,node_modules,.git,third_party,generated}/**",
            2500,
        );
        const entries: ExportedSymbol[] = [];
        // Bound concurrency so a large workspace index does not issue thousands
        // of simultaneous filesystem requests or block the editor event loop.
        for (let start = 0; start < files.length; start += 32) {
            const batch = files.slice(start, start + 32);
            const results = await Promise.all(batch.map(async (file) => {
                try {
                    const bytes = await vscode.workspace.fs.readFile(file);
                    return parseExports(file, Buffer.from(bytes).toString("utf8"));
                } catch {
                    // A file can disappear between findFiles and readFile. Keep
                    // the rest indexed and let the next watcher event retry.
                    return [];
                }
            }));
            for (const result of results) entries.push(...result);
        }
        exportIndex = entries;
    })().finally(() => { indexBuild = undefined; });
    return indexBuild;
}

function relativeModule(from: vscode.Uri, to: vscode.Uri): string {
    let path = vscode.workspace.asRelativePath(to, false);
    const fromDir = from.path.slice(0, from.path.lastIndexOf("/"));
    const targetPath = to.path;
    const fromParts = fromDir.split("/").filter(Boolean);
    const targetParts = targetPath.split("/").filter(Boolean);
    let common = 0;
    while (common < fromParts.length && common < targetParts.length &&
           fromParts[common].toLowerCase() === targetParts[common].toLowerCase()) common++;
    path = [...Array(fromParts.length - common).fill(".."), ...targetParts.slice(common)].join("/");
    if (!path.startsWith(".")) path = `./${path}`;
    return path;
}

function normalizedFile(uri: vscode.Uri): string {
    return uri.fsPath.replace(/\\/g, "/").toLowerCase();
}

// Auto-import is useful while a symbol is being typed, but it must never
// suggest an import into a comment or string. The compiler LSP remains the
// authority for parsing and diagnostics; this scan only protects this editor
// contribution from offering edits in non-code text.
export function isCommentOrStringAt(text: string, offset: number): boolean {
    let state: "code" | "line" | "block" | "single" | "double" | "template" = "code";
    let escaped = false;
    const limit = Math.min(Math.max(offset, 0), text.length);
    for (let i = 0; i < limit; i++) {
        const ch = text[i];
        const next = text[i + 1];
        if (state === "line") {
            if (ch === "\n") state = "code";
            continue;
        }
        if (state === "block") {
            if (ch === "*" && next === "/") { state = "code"; i++; }
            continue;
        }
        if (state === "single" || state === "double" || state === "template") {
            if (escaped) { escaped = false; continue; }
            if (ch === "\\") { escaped = true; continue; }
            if ((state === "single" && ch === "'") ||
                (state === "double" && ch === '"') ||
                (state === "template" && ch === "`")) state = "code";
            continue;
        }
        if (ch === "/" && next === "/") { state = "line"; i++; continue; }
        if (ch === "/" && next === "*") { state = "block"; i++; continue; }
        if (ch === "'") state = "single";
        else if (ch === '"') state = "double";
        else if (ch === "`") state = "template";
    }
    return state !== "code";
}

// Shared with the intelligence engine (src/intelligence/*): these helpers decide
// where an import goes, which names are bound in a document and whether an
// offset sits inside a non-code region. They only read text.
export function importInsertion(document: vscode.TextDocument): vscode.Position {
    let lastImport = -1;
    for (let line = 0; line < document.lineCount; line++) {
        if (importLinePattern.test(document.lineAt(line).text)) lastImport = line;
        importLinePattern.lastIndex = 0;
    }
    return lastImport >= 0
        ? new vscode.Position(lastImport + 1, 0)
        : new vscode.Position(0, 0);
}

export function localBindings(text: string): Set<string> {
    const names = new Set<string>();
    const pattern = /^\s*(?:export\s+)?(?:const|let|var|function|class|interface|enum|type)\s+([A-Za-z_$][\w$]*)/gm;
    for (let match = pattern.exec(text); match; match = pattern.exec(text)) names.add(match[1]);
    const imports = /^\s*import\s+(?:\{([^}]+)\}|([A-Za-z_$][\w$]*))/gm;
    for (let match = imports.exec(text); match; match = imports.exec(text)) {
        if (match[2]) names.add(match[2]);
        if (match[1]) {
            for (const part of match[1].split(",")) {
                const alias = part.trim().split(/\s+as\s+/).pop();
                if (alias) names.add(alias.trim());
            }
        }
    }
    return names;
}

function exportCompletionItems(
    document: vscode.TextDocument,
    position: vscode.Position,
    token: vscode.CancellationToken,
): vscode.CompletionItem[] {
    if (token.isCancellationRequested || document.languageId !== "cp3") return [];
    const linePrefix = document.lineAt(position.line).text.slice(0, position.character);
    const prefix = linePrefix.match(/[A-Za-z_$][\w$]*$/)?.[0] ?? "";
    if (!prefix || /\.\s*[A-Za-z_$\w]*$/.test(linePrefix)) return [];
    const currentText = document.getText();
    // Do not offer imports inside strings, comments or an existing import.
    if (isCommentOrStringAt(currentText, document.offsetAt(position)) ||
        /^\s*import\b/.test(linePrefix)) return [];
    const visibleBindings = localBindings(currentText);
    if (visibleBindings.has(prefix)) return [];

    const openDocuments = vscode.workspace.textDocuments
        .filter((candidate) => candidate.languageId === "cp3" && candidate.uri.scheme === "file");
    const openByPath = new Map(openDocuments.map((candidate) =>
        [normalizedFile(candidate.uri), candidate] as const));
    const openPaths = new Set(openByPath.keys());
    // Disk indexing provides fast workspace discovery. Open documents override
    // that snapshot so unsaved exports appear immediately and deleted unsaved
    // exports do not remain as stale completion entries.
    const availableEntries = exportIndex.filter((entry) => !openPaths.has(normalizedFile(entry.file)));
    for (const open of openDocuments) {
        availableEntries.push(...parseExports(open.uri, open.getText()));
    }
    const byName = new Map<string, { entry: ExportedSymbol; score: number }>();
    for (const entry of availableEntries) {
        if (!entry.name.toLowerCase().startsWith(prefix.toLowerCase()) || entry.name === prefix) continue;
        if (normalizedFile(entry.file) === normalizedFile(document.uri)) continue;
        if (visibleBindings.has(entry.name)) continue;
        const effective = entry;
        const modulePath = relativeModule(document.uri, effective.file);
        const score = modulePath.split("/").filter(Boolean).length +
            modulePath.split("/").filter((part) => part === "..").length * 4;
        const previous = byName.get(effective.name);
        if (!previous || score < previous.score) byName.set(effective.name, { entry: effective, score });
    }

    const candidates = [...byName.values()].sort((a, b) => a.score - b.score || a.entry.name.localeCompare(b.entry.name));
    return candidates.map(({ entry, score }) => {
        const item = new vscode.CompletionItem(entry.name, entry.kind);
        item.insertText = entry.name;
        item.detail = entry.detail;
        item.sortText = `0${String(score).padStart(3, "0")}${entry.name}`;
        const markdown = new vscode.MarkdownString();
        markdown.appendCodeblock(`import { ${entry.name} } from "${relativeModule(document.uri, entry.file)}";`, "cp3");
        if (entry.documentation) markdown.appendMarkdown(`\n${entry.documentation}`);
        item.documentation = markdown;
        item.additionalTextEdits = [new vscode.TextEdit(
            new vscode.Range(importInsertion(document), importInsertion(document)),
            `import { ${entry.name} } from "${relativeModule(document.uri, entry.file)}";\n`,
        )];
        item.tags = [];
        return item;
    });
}

class Cp3OrganizeImportsProvider implements vscode.CodeActionProvider {
    provideCodeActions(document: vscode.TextDocument): vscode.CodeAction[] {
        if (document.languageId !== "cp3") return [];
        const importLines: { line: number; text: string }[] = [];
        for (let line = 0; line < document.lineCount; line++) {
            const text = document.lineAt(line).text;
            if (!/^\s*import\b/.test(text)) continue;
            // Keep side-effect and multiline imports untouched; sorting those
            // can change module initialization order or split a declaration.
            if (/^\s*import\s+["']/.test(text) || !text.includes("from") ||
                text.includes("{") && !text.includes("}")) return [];
            importLines.push({ line, text });
        }
        if (importLines.length < 2) return [];
        const first = importLines[0].line;
        const last = importLines[importLines.length - 1].line;
        if (last - first + 1 !== importLines.length) return [];
        const sorted = [...importLines].sort((a, b) => a.text.localeCompare(b.text));
        if (sorted.every((item, index) => item.text === importLines[index].text)) return [];
        const action = new vscode.CodeAction("CP3: Organize Imports", vscode.CodeActionKind.SourceOrganizeImports);
        const range = new vscode.Range(first, 0, last, document.lineAt(last).range.end.character);
        action.edit = new vscode.WorkspaceEdit();
        action.edit.replace(document.uri, range, sorted.map((item) => item.text).join(document.eol === vscode.EndOfLine.CRLF ? "\r\n" : "\n"));
        return [action];
    }
}

export function registerEditorFeatures(context: vscode.ExtensionContext): void {
    void rebuildExportIndex();
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.cp3");
    const scheduleRebuild = () => {
        if (rebuildTimer) clearTimeout(rebuildTimer);
        rebuildTimer = setTimeout(() => { void rebuildExportIndex(); }, 150);
    };
    watcher.onDidCreate(scheduleRebuild, undefined, context.subscriptions);
    watcher.onDidDelete(scheduleRebuild, undefined, context.subscriptions);
    watcher.onDidChange(scheduleRebuild, undefined, context.subscriptions);
    context.subscriptions.push(watcher);
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((document) => {
        if (document.languageId === "cp3") scheduleRebuild();
    }));
    const identifierTriggers = Array.from(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$",
    );
    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(
        { language: "cp3" },
        { provideCompletionItems: async (document, position, token) => {
            if (!vscode.workspace.getConfiguration("cp3").get<boolean>("suggestWorkspaceExports", true)) return [];
            if (!exportIndex.length) await rebuildExportIndex();
            return exportCompletionItems(document, position, token);
        } },
        ...identifierTriggers,
    ));
    context.subscriptions.push(vscode.languages.registerCodeActionsProvider(
        { language: "cp3" }, new Cp3OrganizeImportsProvider(),
        { providedCodeActionKinds: [vscode.CodeActionKind.SourceOrganizeImports] },
    ));
    context.subscriptions.push(vscode.commands.registerCommand("cp3.organizeImports", async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== "cp3") return;
        const actions = new Cp3OrganizeImportsProvider().provideCodeActions(editor.document);
        if (actions[0]?.edit) await vscode.workspace.applyEdit(actions[0].edit);
    }));
}
