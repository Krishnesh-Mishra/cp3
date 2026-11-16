import * as path from "node:path";
import * as vscode from "vscode";

interface ExportedSymbol {
    readonly name: string;
    readonly kind: vscode.CompletionItemKind;
    readonly file: vscode.Uri;
    readonly detail: string;
    readonly documentation: string;
    // True when the name reaches this file through an `export *` barrel: the
    // barrel is the module's public surface, so importing through it wins a
    // tie against the base file.
    readonly reExport?: boolean;
}

let exportIndex: ExportedSymbol[] = [];
let indexBuild: Promise<void> | undefined;
// Set when a rebuild is requested while another one is running; the running
// build chains one more pass instead of dropping the request.
let rebuildRequested = false;
let rebuildTimer: ReturnType<typeof setTimeout> | undefined;
// When the index was last built. The completion provider refreshes a stale
// index, so a module written a moment ago is offered right away.
let indexBuiltAt = 0;

const exportPattern = /^\s*export\s+(?:(?:declare|default)\s+)*(?:(?:async|abstract)\s+)*(function|class|interface|enum|type|const|let|var)\s+([A-Za-z_$][\w$]*)/gm;
const exportListPattern = /^\s*export\s*\{([^}]+)\}(?:\s*from\s*["'][^"']+["'])?\s*;?/gm;
// `export default handler;` - the default export of a local declaration.
const defaultIdentifierPattern = /^\s*export\s+default\s+([A-Za-z_$][\w$]*)\s*;?\s*$/gm;
// `export * from "./base.cp3";` - the barrel re-exports another module.
const starExportPattern = /^\s*export\s+\*\s+from\s+["']([^"']+)["']\s*;?\s*$/gm;
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
    // `export default handler;` exports the local declaration under its own
    // name, so the name is importable.
    defaultIdentifierPattern.lastIndex = 0;
    for (let match = defaultIdentifierPattern.exec(text); match; match = defaultIdentifierPattern.exec(text)) {
        const name = match[1];
        if (entries.some((entry) => entry.name === name)) continue;
        entries.push({
            name,
            kind: vscode.CompletionItemKind.Variable,
            file: uri,
            detail: `default export - ${vscode.workspace.asRelativePath(uri, false)}`,
            documentation: "",
        });
    }
    return entries;
}

async function rebuildExportIndex(): Promise<void> {
    if (indexBuild) {
        // A build is already running. Ask for another one after it instead of
        // dropping the request: a file created while the running build was
        // scanning would otherwise stay unindexed until the next file event,
        // and the auto-import would keep offering a stale module.
        rebuildRequested = true;
        return indexBuild;
    }
    for (;;) {
        rebuildRequested = false;
        indexBuild = buildExportIndex();
        try {
            await indexBuild;
        } finally {
            indexBuild = undefined;
        }
        if (!rebuildRequested) return;
    }
}

async function buildExportIndex(): Promise<void> {
    {
        const files = await vscode.workspace.findFiles(
            "**/*.cp3",
            "**/{build,dist,node_modules,.git,third_party,generated}/**",
            2500,
        );
        const entries: ExportedSymbol[] = [];
        // The exports of every file, keyed by its normalized path, so a
        // barrel's `export * from` can be resolved after the first pass.
        const perFile = new Map<string, { uri: vscode.Uri; parsed: ExportedSymbol[] }>();
        const texts = new Map<string, string>();
        // Bound concurrency so a large workspace index does not issue thousands
        // of simultaneous filesystem requests or block the editor event loop.
        for (let start = 0; start < files.length; start += 32) {
            const batch = files.slice(start, start + 32);
            const results = await Promise.all(batch.map(async (file) => {
                try {
                    const bytes = await vscode.workspace.fs.readFile(file);
                    const text = Buffer.from(bytes).toString("utf8");
                    return { file, text, parsed: parseExports(file, text) };
                } catch {
                    // A file can disappear between findFiles and readFile. Keep
                    // the rest indexed and let the next watcher event retry.
                    return undefined;
                }
            }));
            for (const result of results) {
                if (!result) continue;
                const key = normalizedFile(result.file);
                perFile.set(key, { uri: result.file, parsed: result.parsed });
                texts.set(key, result.text);
                entries.push(...result.parsed);
            }
        }
        // A barrel re-exports another module: the names it exports are
        // importable through the barrel itself.
        for (const [key, text] of texts) {
            starExportPattern.lastIndex = 0;
            for (let match = starExportPattern.exec(text); match; match = starExportPattern.exec(text)) {
                try {
                    const target = resolveRelativeModule(perFile.get(key)!.uri, match[1], perFile);
                    if (!target) continue;
                    const reexported = perFile.get(normalizedFile(target));
                    if (!reexported) continue;
                    const barrel = perFile.get(key)!.uri;
                    for (const entry of reexported.parsed) {
                        entries.push({ ...entry, file: barrel, reExport: true });
                    }
                } catch (error) {
                    console.error("cp3: export-star resolution failed", error);
                }
            }
        }
        exportIndex = entries;
        indexBuiltAt = Date.now();
    }
}

// The file a relative module specifier names, or undefined when it does not
// resolve to a file of the workspace.
function resolveRelativeModule(
    from: vscode.Uri,
    specifier: string,
    known: Map<string, { uri: vscode.Uri; parsed: ExportedSymbol[] }>,
): vscode.Uri | undefined {
    if (!specifier.startsWith(".")) return undefined;
    const base = path.dirname(from.fsPath);
    const target = path.resolve(base, specifier);
    for (const candidate of [target, `${target}.cp3`]) {
        const uri = vscode.Uri.file(candidate);
        if (known.has(normalizedFile(uri))) return uri;
    }
    return undefined;
}

// The project the edited file belongs to: the nearest ancestor directory with
// a package.json (a project root), or the workspace folder when the project
// has no manifest. The auto-import offers the modules of that project only -
// a module of an unrelated project in the same workspace is not importable
// from here.
async function projectRootOf(document: vscode.Uri): Promise<string | undefined> {
    const folder = vscode.workspace.getWorkspaceFolder(document);
    const root = folder?.uri.fsPath;
    let dir = path.dirname(document.fsPath);
    for (;;) {
        try {
            await vscode.workspace.fs.stat(vscode.Uri.file(path.join(dir, "package.json")));
            return dir;
        } catch {
            // Not a project root: keep walking up.
        }
        if (root && path.resolve(dir) === path.resolve(root)) return root;
        const parent = path.dirname(dir);
        if (parent === dir) return root;
        dir = parent;
    }
}

function isInsideProject(root: string | undefined, file: vscode.Uri): boolean {
    if (!root) return true;
    const relative = path.relative(root, file.fsPath);
    return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
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
    // A directory index is imported through its folder (`./mathx`), not
    // through the index file itself (`./mathx/index.cp3`).
    if (path.endsWith("/index.cp3")) path = path.slice(0, -"/index.cp3".length);
    if (!path.startsWith(".")) path = `./${path}`;
    return path;
}

function normalizedFile(uri: vscode.Uri): string {
    return uri.fsPath.replace(/\\/g, "/").toLowerCase();
}

// The compiler already writes the fix for its own diagnostics, and its
// messages carry it: "'console' has no member 'lgoh'; did you mean 'log'?",
// "cannot find name 'x'; add `import x ...`". The editor reads the same
// message and offers them as quick fixes. A suggestion the compiler does
// not emit cannot be offered; a program with no diagnostics gets no
// quick fixes.
interface FixSuggestion {
    /** What the editor shows; always mentions the names involved. */
    readonly title: string;
    readonly replacement?: string;
    readonly importLine?: string;
    /** An identifier to remove its import line for, when the compiler
     * reports "X is a language builtin, no import is needed". */
    readonly dropImportLineIdentifier?: string;
}

/** The names after "did you mean" up to the end of the sentence. */
function didYouMeanNames(text: string): string[] {
    const at = text.indexOf("did you mean ");
    if (at < 0) return [];
    const tail = text.slice(at + "did you mean ".length);
    const out: string[] = [];
    for (const match of tail.matchAll(/'([A-Za-z_$][A-Za-z0-9_$]*)'/g)) {
        out.push(match[1]);
    }
    return out;
}

/** The import the compiler suggests adding ("add `import ...`"). */
function suggestedImportLine(text: string): string | undefined {
    const match = /add `(import [^`]+)`/.exec(text);
    return match?.[1];
}

function identifierRangeAt(
    document: vscode.TextDocument,
    fromOffset: number,
    name: string,
): vscode.Range | undefined {
    const text = document.getText();
    const pattern = new RegExp(`(?<![A-Za-z0-9_$])${name.replace(/[$]/g, "\\$")}(?![A-Za-z0-9_$])`, "g");
    pattern.lastIndex = fromOffset;
    const match = pattern.exec(text);
    if (!match) return undefined;
    const start = document.positionAt(match.index);
    const end = document.positionAt(match.index + name.length);
    return new vscode.Range(start, end);
}

function fixesForDiagnostic(
    document: vscode.TextDocument,
    diagnostic: vscode.Diagnostic,
): FixSuggestion[] {
    const suggestions: FixSuggestion[] = [];
    const message = diagnostic.message;
    // "Add `import ...`" is a full-sentence suggestion: offer it as its own
    // action (the editor inserts the line at the top).
    const importLine = suggestedImportLine(message);
    if (importLine) {
        suggestions.push({ title: importLine, importLine });
    }
    // "has no member 'x'" / "cannot find name 'x'" with "did you mean 'y'?":
    // one action per alternative, applying the replacement.
    const wrong = /(?:has no member|has no name|cannot find name) '([A-Za-z_$][A-Za-z0-9_$]*)'/.exec(message);
    const alternatives = didYouMeanNames(message);
    if (wrong && alternatives.length > 0) {
        const current = wrong[1];
        for (const replacement of alternatives) {
            suggestions.push({
                title: `Replace '${current}' with '${replacement}'`,
                replacement,
            });
        }
    }
    // "EventEmitter is a language builtin, no import is needed": offer to
    // drop the import line. The message names the builtin ("... EventEmitter is
    // a language builtin ...").
    if (diagnostic.code === "CP3601" && message.includes("is a language builtin")) {
        const builtin = /([A-Za-z_$][A-Za-z0-9_$]*) is a language builtin/.exec(message);
        if (builtin) {
            suggestions.push({
                title: `Use the built-in ${builtin[1]} instead`,
                dropImportLineIdentifier: builtin[1],
            });
        }
    }
    // "unused import 'x'" diagnostics carry no suggestions yet: the compiler
    // does not emit an unused-import suggestion, so nothing is offered.
    return suggestions;
}

/** Names bound in a simple import line, or [] for non-imports. */
function boundNamesFromImportLine(text: string): string[] {
    const match = /^(\s*)import\s+([^;]+?)\s+from\s+["']([^"']+)["']\s*;?\s*$/.exec(text);
    if (!match) return [];
    const clause = match[2].trim();
    const names: string[] = [];
    if (clause.startsWith("{")) {
        for (const part of clause.replace(/^\{|\}$/g, "").split(",")) {
            const alias = part.trim().split(/\s+as\s+/).pop()?.trim();
            if (alias && /^[A-Za-z_$][\w$]*$/.test(alias)) names.push(alias);
        }
        return names;
    }
    const star = clause.match(/^\*\s+as\s+([A-Za-z_$][\w$]*)/);
    if (star) { names.push(star[1]); return names; }
    const defaultPart = clause.split(",", 1)[0].trim();
    if (/^[A-Za-z_$][\w$]*$/.test(defaultPart)) names.push(defaultPart);
    const namedMatch = clause.match(/\{([^}]*)\}/);
    if (namedMatch) {
        for (const part of namedMatch[1].split(",")) {
            const alias = part.trim().split(/\s+as\s+/).pop()?.trim();
            if (alias && /^[A-Za-z_$][\w$]*$/.test(alias)) names.push(alias);
        }
    }
    return names;
}

/** True if `name` is referenced anywhere in `text` outside its own import
 * line and outside comments and strings. */
function identifierReferencedOutside(text: string, name: string, exceptLine: number): boolean {
    const pattern = new RegExp(`(?<![A-Za-z0-9_$])${name.replace(/[$]/g, "\\$")}(?![A-Za-z0-9_$])`, "g");
    for (const match of text.matchAll(pattern)) {
        const before = text.slice(0, match.index);
        const line = before.split("\n").length;
        if (line === exceptLine + 1) continue;
        if (isCommentOrStringAt(text, match.index)) continue;
        return true;
    }
    return false;
}

/** Unused import suggestions inside the cursor line range: one action per
 * fully-unused import statement covering the whole line. */
function unusedImportActions(document: vscode.TextDocument, range: vscode.Range | vscode.Selection): vscode.CodeAction[] {
    const text = document.getText();
    const actions: vscode.CodeAction[] = [];
    const startLine = range.start.line;
    const endLine = range.end.line;
    for (let line = 0; line < document.lineCount; line++) {
        if (line < startLine || line > endLine) continue;
        const lineText = document.lineAt(line).text;
        if (!/^\s*import\s+/.test(lineText)) continue;
        const names = boundNamesFromImportLine(lineText);
        if (names.length === 0) continue;
        const unused = names.filter((name) => !identifierReferencedOutside(text, name, line));
        if (unused.length !== names.length) continue;
        const action = new vscode.CodeAction(`remove unused import`, vscode.CodeActionKind.QuickFix);
        action.edit = new vscode.WorkspaceEdit();
        action.edit.delete(document.uri, new vscode.Range(line, 0, line + 1, 0));
        actions.push(action);
    }
    return actions;
}

function importLineAtTop(document: vscode.TextDocument): vscode.Position {
    return importInsertion(document);
}

function dropImportLineFor(document: vscode.TextDocument, identifier: string): vscode.Range | undefined {
    const pattern = new RegExp(`^\\s*import[^\\n]*\\b${identifier}\\b`);
    for (let line = 0; line < document.lineCount; line++) {
        if (pattern.test(document.lineAt(line).text)) {
            return new vscode.Range(line, 0, line + 1, 0);
        }
    }
    return undefined;
}

class Cp3QuickFixProvider implements vscode.CodeActionProvider {
    provideCodeActions(document: vscode.TextDocument, range: vscode.Range | vscode.Selection): vscode.CodeAction[] {
        if (document.languageId !== "cp3") return [];
        const actions: vscode.CodeAction[] = [];
        for (const diagnostic of vscode.languages.getDiagnostics(document.uri)) {
            const offset = document.offsetAt(diagnostic.range.start);
            for (const fix of fixesForDiagnostic(document, diagnostic)) {
                const action = new vscode.CodeAction(fix.title, vscode.CodeActionKind.QuickFix);
                action.diagnostics = [diagnostic];
                action.edit = new vscode.WorkspaceEdit();
                if (fix.replacement) {
                    const target = identifierRangeAt(document, offset, findWrongName(document, diagnostic));
                    if (target) action.edit.replace(document.uri, target, fix.replacement);
                } else if (fix.importLine) {
                    action.edit.insert(document.uri, importLineAtTop(document), `${fix.importLine}\n`);
                } else if (fix.dropImportLineIdentifier) {
                    const drop = dropImportLineFor(document, fix.dropImportLineIdentifier);
                    if (drop) action.edit.delete(document.uri, drop);
                }
                actions.push(action);
            }
        }
        // Unused imports: always offer to remove them (the compiler is silent
        // about them, so the fix is provided directly off the text).
        actions.push(...unusedImportActions(document, range));
        return actions;
    }
}

/** The name the compiler is about ("has no member 'x'"). */
function findWrongName(document: vscode.TextDocument, diagnostic: vscode.Diagnostic): string {
    const wrong = /(?:has no member|has no name|cannot find name) '([A-Za-z_$][A-Za-z0-9_$]*)'/.exec(diagnostic.message);
    return wrong?.[1] ?? "";
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

// The exported names the document already imports, per module (resolved
// against the document's folder). A name the document already imports from a
// module is not offered again - not even under another alias.
function importedNamesByModule(document: vscode.TextDocument): Map<string, Set<string>> {
    const out = new Map<string, Set<string>>();
    const pattern = /^\s*import\s+([^;]*?)\s+from\s+["']([^"']+)["']/gm;
    const text = document.getText();
    pattern.lastIndex = 0;
    for (let match = pattern.exec(text); match; match = pattern.exec(text)) {
        const clause = match[1].trim();
        const specifier = match[2];
        if (!specifier.startsWith(".")) continue;
        const resolved = path.resolve(path.dirname(document.uri.fsPath), specifier);
        const names = out.get(normalizedFile(vscode.Uri.file(resolved))) ?? new Set<string>();
        const named = clause.match(/\{([^}]*)\}/);
        if (named) {
            for (const part of named[1].split(",")) {
                const names_ = part.trim().split(/\s+as\s+/);
                const exported = (names_[0] ?? "").trim();
                if (exported) names.add(exported);
            }
        } else {
            // A default or namespace import binds the module's default
            // export; the name it covers is not known here.
            names.add("default");
        }
        out.set(normalizedFile(vscode.Uri.file(resolved)), names);
    }
    return out;
}

async function exportCompletionItems(
    document: vscode.TextDocument,
    position: vscode.Position,
    token: vscode.CancellationToken,
): Promise<vscode.CompletionItem[]> {
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
    // Only the modules of the edited file's project are importable from here.
    const projectRoot = await projectRootOf(document.uri);
    const alreadyImported = importedNamesByModule(document);
    const byName = new Map<string, { entry: ExportedSymbol; score: number; reExport: number }>();
    for (const entry of availableEntries) {
        if (!entry.name.toLowerCase().startsWith(prefix.toLowerCase()) || entry.name === prefix) continue;
        if (normalizedFile(entry.file) === normalizedFile(document.uri)) continue;
        if (!isInsideProject(projectRoot, entry.file)) continue;
        if (alreadyImported.get(normalizedFile(entry.file))?.has(entry.name)) continue;
        if (visibleBindings.has(entry.name)) continue;
        const effective = entry;
        const modulePath = relativeModule(document.uri, effective.file);
        const score = modulePath.split("/").filter(Boolean).length +
            modulePath.split("/").filter((part) => part === "..").length * 4;
        const reExport = effective.reExport ? 1 : 0;
        const previous = byName.get(effective.name);
        if (!previous || score < previous.score ||
            (score === previous.score && reExport > previous.reExport)) {
            byName.set(effective.name, { entry: effective, score, reExport });
        }
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
            // Refresh a stale index: a module written a moment ago (a file the
            // watcher has not reported yet, or a file created while a build
            // was running) must be offered right away.
            if (!exportIndex.length || Date.now() - indexBuiltAt > 1_500) {
                void rebuildExportIndex();
            }
            return exportCompletionItems(document, position, token);
        } },
        ...identifierTriggers,
    ));
    context.subscriptions.push(vscode.languages.registerCodeActionsProvider(
        { language: "cp3" }, new Cp3QuickFixProvider(),
        { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] },
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
