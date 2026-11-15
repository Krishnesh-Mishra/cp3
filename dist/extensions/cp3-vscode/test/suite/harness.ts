// test/suite/harness.ts
//
// Shared helpers for the extension-host test cases. The cases run inside a
// real VS Code instance launched by @vscode/test-electron (see test/runTest.mjs
// and test/run.cjs); they open real .cp3 fixture files on disk and ask VS Code
// for completions/definitions/etc. through the built-in `_execute*Provider`
// commands (the underscore-prefixed internal variants this VS Code registers;
// `getCommands` does not list them), which routes to the cp3 language server
// exactly the way the editor does.
//
// Fixtures are written into the run's throwaway workspace folder so the
// extension's workspace-export index and the server's project model see them;
// each case gets its own file under cases/<suite>/ to keep imports isolated.
// The `__CURSOR__` marker in a fixture source string marks the position the
// case cares about; openFixture strips it and computes the position.

import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import { createRequire } from "node:module";
import * as vscode from "vscode";

// ---------------------------------------------------------------------------
// Repository / binary resolution
// ---------------------------------------------------------------------------

function repoRoot(): string {
    let dir = __dirname;
    for (let depth = 0; depth < 12; depth += 1) {
        if (
            fs.existsSync(path.join(dir, "CMakeLists.txt")) &&
            fs.existsSync(path.join(dir, "src", "cli", "CMakeLists.txt"))
        ) {
            return dir;
        }
        const parent = path.dirname(dir);
        if (parent === dir) {
            break;
        }
        dir = parent;
    }
    throw new Error(`cannot locate the cp3 repository root from ${__dirname}`);
}

export function findCp3c(): string {
    const root = repoRoot();
    const candidates =
        process.platform === "win32"
            ? [
                  path.join(root, "build", "windows-release", "bin", "cp3c.exe"),
                  path.join(root, "build", "out", "windows-x64", "stage", "cp3", "bin", "cp3c.exe"),
                  path.join(root, "build", "bin", "cp3c.exe"),
              ]
            : [
                  path.join(root, "build", "linux-release", "bin", "cp3c"),
                  path.join(root, "build", "bin", "cp3c"),
              ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    throw new Error(`no cp3c binary found; tried:\n${candidates.join("\n")}`);
}

/** Pin `cp3.compilerPath` at the repository's release binary. Idempotent. */
export async function ensureCompilerConfigured(): Promise<void> {
    const configured = vscode.workspace.getConfiguration("cp3").get<string>("compilerPath");
    if (configured && fs.existsSync(configured) && configured !== "cp3c") {
        return;
    }
    await vscode.workspace
        .getConfiguration("cp3")
        .update("compilerPath", findCp3c(), vscode.ConfigurationTarget.Global);
}

let serverWarmed = false;

/**
 * Starts the language server exactly once for this run. Explicitly asking for
 * it is deterministic: relying on activation order is racy in an empty
 * workspace, because activation happens during the fixture's first open, at
 * which point the workspace may still see no `.cp3` file. Every LSP case
 * calls this before touching the server; repeated calls are no-ops.
 *
 * The server needs a moment to load and check its first document, so a
 * warm-up fixture is opened and polled until the server reports a diagnostic
 * for it: that is the only deterministic way to know the server is ready to
 * answer queries for the fixtures that follow.
 */
export async function ensureServerStarted(): Promise<void> {
    await ensureCompilerConfigured();
    if (serverWarmed) {
        return;
    }
    serverWarmed = true;
    await vscode.commands.executeCommand("cp3.restartServer");
    const root = workspaceRoot();
    if (!root) {
        return;
    }
    const warmupFile = path.join(root, ".cp3-warmup.cp3");
    const warmupSource = "function warmupBroken( {\n  return 1;\n}\n";
    fs.writeFileSync(warmupFile, warmupSource, "utf8");
    try {
        const document = await vscode.workspace.openTextDocument(vscode.Uri.file(warmupFile));
        await vscode.window.showTextDocument(document, { preview: true });
        const deadline = Date.now() + 20_000;
        for (;;) {
            const found = vscode.languages.getDiagnostics(document.uri);
            if (found.length > 0) {
                break;
            }
            if (Date.now() > deadline) {
                break;  // The server may be slow; do not stall the whole run.
            }
            await new Promise((resolve) => setTimeout(resolve, 200));
        }
        await vscode.commands.executeCommand("workbench.action.closeActiveEditor");
    } finally {
        fs.rmSync(warmupFile, { force: true });
    }
}

// ---------------------------------------------------------------------------
// Fixtures (in-workspace, so cross-file cases resolve imports)
// ---------------------------------------------------------------------------

let caseCounter = 0;

function workspaceRoot(): string | undefined {
    return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

/** The `__CURSOR__` marker in a fixture source marks the position under test. */
export function fixtureWithCursor(source: string): { content: string; line: number; character: number } {
    const index = source.indexOf("__CURSOR__");
    if (index < 0) {
        throw new Error("fixture is missing the __CURSOR__ marker");
    }
    const content = source.slice(0, index) + source.slice(index + "__CURSOR__".length);
    const before = content.slice(0, index);
    const line = before.split("\n").length - 1;
    const character = index - (before.lastIndexOf("\n") + 1);
    return { content, line, character };
}

/** Writes `content` under cases/<suite>/<name> and returns its URI. */
export function writeWorkspaceCase(name: string, content: string, suite = "cases"): vscode.Uri {
    const root = workspaceRoot();
    if (!root) {
        throw new Error("the test run needs a workspace folder to write .cp3 fixtures into");
    }
    const dir = path.join(root, "cases", suite);
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, name);
    fs.writeFileSync(file, content);
    return vscode.Uri.file(file);
}

export async function openWorkspaceCase(
    name: string,
    content: string,
    suite = "cases",
): Promise<vscode.TextDocument> {
    await ensureCompilerConfigured();
    return vscode.workspace.openTextDocument(writeWorkspaceCase(name, content, suite));
}

/** Removes the cases/ tree written during this run (best effort: VS Code
 * watches files, so deletion can fail transiently on Windows). */
export function cleanupFixtures(): void {
    const root = workspaceRoot();
    if (!root) {
        return;
    }
    try {
        fs.rmSync(path.join(root, "cases"), { recursive: true, force: true, maxRetries: 6, retryDelay: 150 });
    } catch (error) {
        console.error(`[cp3-tests] could not remove fixtures: ${String(error)}`);
    }
}

// ---------------------------------------------------------------------------
// Generic polling
// ---------------------------------------------------------------------------

export async function waitFor<T>(
    fn: () => Promise<T | undefined>,
    accept: (value: T) => boolean,
    timeoutMs = 6_000,
    intervalMs = 300,
): Promise<T> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
        const value = await fn().catch(() => undefined);
        if (value !== undefined && accept(value)) {
            return value;
        }
        if (Date.now() > deadline) {
            throw new Error(`timed out after ${timeoutMs}ms waiting for a condition`);
        }
        await new Promise((resolve) => setTimeout(resolve, intervalMs));
    }
}

export function completionLabel(item: vscode.CompletionItem): string {
    return typeof item.label === "string" ? item.label : item.label.label;
}

/** Normalizes VS Code's internal range shapes ({startLineNumber, startColumn,
 * endLineNumber, endColumn} and LSP {start:{line,character},…}) into one
 * 0-based {start,end} shape with line/character. */
export function normalizeRange(range: unknown): { start: { line: number; character: number }; end: { line: number; character: number } } | undefined {
    const anyRange = range as {
        start?: { line?: number; character?: number };
        end?: { line?: number; character?: number };
        startLineNumber?: number;
        startColumn?: number;
        endLineNumber?: number;
        endColumn?: number;
    } | undefined;
    if (anyRange === undefined) {
        return undefined;
    }
    if (anyRange.start !== undefined && typeof anyRange.start.line === "number") {
        return anyRange as {
            start: { line: number; character: number };
            end: { line: number; character: number };
        };
    }
    if (typeof anyRange.startLineNumber === "number") {
        return {
            start: { line: anyRange.startLineNumber - 1, character: (anyRange.startColumn ?? 1) - 1 },
            end: { line: (anyRange.endLineNumber ?? 1) - 1, character: (anyRange.endColumn ?? 1) - 1 },
        };
    }
    return undefined;
}

/** One completion request; never throws. */
export async function completionLabels(
    document: vscode.TextDocument,
    position: vscode.Position,
): Promise<string[] | undefined> {
    // VS Code's internal completion command resolves completions in
    // `{ incomplete, suggestions: CompletionItem[] }` form, not the public
    // CompletionList `{ isIncomplete, items }` shape.
    const internal = await vscode.commands
        .executeCommand<{ incomplete?: boolean; suggestions?: vscode.CompletionItem[] }>(
            "_executeCompletionItemProvider",
            document.uri,
            position,
        )
        .then((value) => value, (error) => {
            console.error(`[cp3-tests] completion command failed: ${String(error)}`);
            return undefined;
        });
    if (internal === undefined) {
        return undefined;
    }
    const items = internal.suggestions ?? (internal as unknown as vscode.CompletionList).items ?? [];
    // The internal command returns the provider's full list; VS Code's editor
    // filters it by the current-word prefix client-side. Emulate that so
    // expectations match what the user actually sees.
    const lineText = document.lineAt(position.line).text.slice(0, position.character);
    const prefix = (lineText.match(/[A-Za-z_$][\w$]*$/)?.[0] ?? "").toLowerCase();
    const filtered = prefix
        ? items.filter((item) => completionLabel(item).toLowerCase().startsWith(prefix))
        : items;
    if (process.env["CP3_TEST_DEBUG"] === "1") {
        console.log(`[cp3-tests] completion(${document.uri.fsPath}:${position.line}:${position.character}): ${filtered.length} of ${items.length} item(s) (prefix=${JSON.stringify(prefix)})`);
    }
    return filtered.map(completionLabel);
}

/**
 * The language server needs a moment to start and to parse the document, so
 * poll until `wanted` all appear (or the budget runs out). When `wanted` is
 * empty, returns once the server answers the same labels twice in a row so
 * "expect nothing" assertions see a stable list rather than the first empty
 * response during server startup.
 */
export async function waitForCompletions(
    document: vscode.TextDocument,
    position: vscode.Position,
    wanted: readonly string[],
    timeoutMs = 8_000,
): Promise<string[]> {
    if (wanted.length > 0) {
        const deadline = Date.now() + timeoutMs;
        let last: string[] = [];
        let answered = 0;
        for (;;) {
            const labels = await completionLabels(document, position).catch(() => undefined);
            if (labels !== undefined) {
                last = labels;
                answered += 1;
                if (wanted.every((name) => labels.includes(name))) {
                    return labels;
                }
                // The server answered but the wanted items are not there;
                // polling longer will not conjure them, so fail fast with
                // what it actually said.
                if (answered >= 8) {
                    throw new Error(
                        `completions did not include ${JSON.stringify([...wanted])}; ` +
                            `last response: ${JSON.stringify(labels)}`,
                    );
                }
            }
            if (Date.now() > deadline) {
                throw new Error(
                    `timed out after ${timeoutMs}ms waiting for completions ${JSON.stringify([...wanted])}; ` +
                        `last response: ${JSON.stringify(last)}`,
                );
            }
            await new Promise((resolve) => setTimeout(resolve, 300));
        }
    }
    const deadline = Date.now() + timeoutMs;
    let last: string[] | undefined;
    for (;;) {
        const labels = await completionLabels(document, position).catch(() => undefined);
        if (labels !== undefined) {
            const previous = last;
            if (previous !== undefined && labels.length === previous.length && labels.every((l, i) => l === previous[i])) {
                return labels;
            }
            last = labels;
        }
        if (Date.now() > deadline) {
            return last ?? [];
        }
        await new Promise((resolve) => setTimeout(resolve, 400));
    }
}

/** Optional companion modules a case needs (relative to the fixture's folder): [path, content][] */
export interface CaseModules {
    readonly modules?: ReadonlyArray<readonly [path: string, content: string]>;
    /** Place the main fixture in a subdirectory (defaults to the case root). */
    readonly mainPath?: string;
}

interface NamedSource extends CaseModules {
    readonly name: string;
    readonly source: string;
}

function slugify(text: string): string {
    return text.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 60);
}

async function openCaseDocument(spec: NamedSource, folder: string): Promise<vscode.TextDocument> {
    // A fixture may carry the __CURSOR__ marker; the file on disk must not,
    // or the server sees it as a (partial) identifier being typed.
    const source = spec.source.includes("__CURSOR__")
        ? fixtureWithCursor(spec.source).content
        : spec.source;
    const workspace = workspaceRoot();
    if (!workspace) {
        throw new Error("the test run needs a workspace folder to write .cp3 fixtures into");
    }
    const unique = `${slugify(spec.name) || "case"}-${(caseCounter += 1)}`;
    const root = path.join(workspace, "cases", folder, unique);
    fs.mkdirSync(root, { recursive: true });
    const rootWithSep = root.endsWith(path.sep) ? root : root + path.sep;
    for (const [modulePath, moduleContent] of spec.modules ?? []) {
        const target = path.resolve(root, modulePath.replace(/^\.\//, ""));
        if (!target.startsWith(rootWithSep)) {
            throw new Error(`module path escapes the case directory: ${modulePath}`);
        }
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, moduleContent);
    }
    const mainFile = path.resolve(root, spec.mainPath ?? `${unique}.cp3`);
    if (!mainFile.startsWith(rootWithSep)) {
        throw new Error(`mainPath escapes the case directory: ${spec.mainPath}`);
    }
    // The case folder is its own project: the extension offers auto-import
    // candidates from the project of the edited file, so a case must not see
    // the modules the other cases wrote into the same workspace.
    fs.writeFileSync(
        path.join(root, "package.json"),
        JSON.stringify({ name: unique, private: true }, null, 2),
    );
    fs.mkdirSync(path.dirname(mainFile), { recursive: true });
    fs.writeFileSync(mainFile, source);
    await ensureCompilerConfigured();
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(mainFile));
    // The shared LSP drivers warm the server deterministically; fixtures
    // written here need one final poll-free call to keep cases fast.
    await ensureServerStarted();
    return document;
}

export interface CompletionCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly expectPresent?: readonly string[];
    readonly expectAbsent?: readonly string[];
    readonly timeoutMs?: number;
}

export function completionCases(suiteName: string, cases: readonly CompletionCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "completion");
                const position = new vscode.Position(line, character);
                const wanted = spec.expectPresent ?? [];
                const labels = await waitForCompletions(document, position, wanted, spec.timeoutMs);
                for (const name of spec.expectPresent ?? []) {
                    assert.ok(labels.includes(name), `expected ${JSON.stringify(name)} in completions; got: ${labels.join(", ")}`);
                }
                for (const name of spec.expectAbsent ?? []) {
                    assert.ok(!labels.includes(name), `expected ${JSON.stringify(name)} absent; got: ${labels.join(", ")}`);
                }
            });
        }
    });
}

export interface HoverCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** Substrings that must appear in the flattened hover text. */
    readonly contains?: readonly string[];
    /** When true, the provider must answer null/undefined/empty contents. */
    readonly expectNull?: boolean;
}

function flattenHover(result: unknown): string {
    const hovers = Array.isArray(result) ? result : result ? [result] : [];
    const out: string[] = [];
    for (const hover of hovers as vscode.Hover[]) {
        for (const part of hover.contents ?? []) {
            if (typeof part === "string") {
                out.push(part);
            } else if (part instanceof vscode.MarkdownString) {
                out.push(part.value);
            } else if (part && typeof part === "object" && "value" in part) {
                out.push(String((part as { value: unknown }).value));
            }
        }
    }
    return out.join("\n");
}

export function hoverCases(suiteName: string, cases: readonly HoverCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "hover");
                const position = new vscode.Position(line, character);
                const raw = await waitFor(
                    () =>
                        vscode.commands.executeCommand(
                            "_executeHoverProvider",
                            document.uri,
                            position,
                        ) as Promise<vscode.Hover | vscode.Hover[] | undefined>,
                    (value) => value !== undefined,
                    20_000,
                ).catch(() => undefined);
                const text = flattenHover(raw);
                if (spec.expectNull) {
                    assert.equal(text.trim(), "", `expected no hover; got: ${text}`);
                    return;
                }
                for (const chunk of spec.contains ?? []) {
                    assert.ok(text.includes(chunk), `expected ${JSON.stringify(chunk)} in hover; got: ${text}`);
                }
            });
        }
    });
}

interface LocationLike {
    readonly uri: vscode.Uri;
    readonly range: unknown;
}

/** The identifier under a position (letters/digits/_/$ run), or undefined. */
function identifierAtPosition(content: string, line: number, character: number): string | undefined {
    const text = content.split("\n")[line];
    if (text === undefined) return undefined;
    const isWord = (c: string) => /[A-Za-z0-9_$]/.test(c);
    let start = character;
    while (start > 0 && isWord(text[start - 1])) start -= 1;
    let end = character;
    while (end < text.length && isWord(text[end])) end += 1;
    if (start === end) return undefined;
    return text.slice(start, end);
}

/** The range of the first whole-word occurrence of `word` (the declaration for the local-symbol fixtures). */
function firstOccurrenceRange(
    content: string,
    word: string,
): { start: { line: number; character: number }; end: { line: number; character: number } } | undefined {
    const escaped = word.replace(/[$]/g, "\\$");
    const match = new RegExp(`(?<![A-Za-z0-9_$])${escaped}(?![A-Za-z0-9_$])`).exec(content);
    if (!match) return undefined;
    const before = content.slice(0, match.index);
    const line = before.split("\n").length - 1;
    const character = match.index - (before.lastIndexOf("\n") + 1);
    return { start: { line, character }, end: { line, character: character + word.length } };
}

function targetLines(result: unknown): number[] {
    const locations = (Array.isArray(result) ? result : result ? [result] : []) as LocationLike[];
    const lines: number[] = [];
    for (const location of locations) {
        const range = normalizeRange(location.range);
        if (range) {
            lines.push(range.start.line);
        }
    }
    return lines;
}

export interface DefinitionCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** 0-based lines the definition must land on (any match suffices). */
    readonly expectLines?: readonly number[];
    /** When set, expect exactly this many locations. */
    readonly expectCount?: number;
    readonly expectNone?: boolean;
}

function definitionDriver(
    command: string,
    suiteName: string,
    cases: readonly DefinitionCase[],
    folder: string,
): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, folder);
                const position = new vscode.Position(line, character);
                const raw = await waitFor(
                    () =>
                        vscode.commands.executeCommand(command, document.uri, position) as Promise<
                            LocationLike | LocationLike[] | undefined
                        >,
                    (value) => value !== undefined,
                    6_000,
                ).catch(() => undefined);
                const lines = targetLines(raw);
                if (spec.expectNone) {
                    assert.equal(lines.length, 0, `expected no locations; got lines: ${lines.join(",")}`);
                    return;
                }
                if (spec.expectCount !== undefined) {
                    assert.equal(lines.length, spec.expectCount, `expected ${spec.expectCount} location(s); got ${lines.length}`);
                }
                if (spec.expectLines) {
                    assert.ok(
                        spec.expectLines.some((wanted) => lines.includes(wanted)),
                        `expected a definition on line ${spec.expectLines.join(" or ")}; got ${lines.join(",") || "none"}`,
                    );
                }
            });
        }
    });
}

export function definitionCases(suiteName: string, cases: readonly DefinitionCase[]): void {
    definitionDriver("_executeDefinitionProvider", suiteName, cases, "definition");
}

export function declarationCases(suiteName: string, cases: readonly DefinitionCase[]): void {
    definitionDriver("_executeDeclarationProvider", suiteName, cases, "declaration");
}

export function typeDefinitionCases(suiteName: string, cases: readonly DefinitionCase[]): void {
    definitionDriver("_executeTypeDefinitionProvider", suiteName, cases, "typedef");
}

export function implementationCases(suiteName: string, cases: readonly DefinitionCase[]): void {
    definitionDriver("_executeImplementationProvider", suiteName, cases, "impl");
}

export interface ReferencesCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** 0-based lines the references must land on; every entry must match. */
    readonly expectAllLines?: readonly number[];
    /** Lines that must appear at least once. */
    readonly expectSomeLines?: readonly number[];
    readonly expectCount?: number;
    readonly expectNone?: boolean;
    readonly includeDeclaration?: boolean;
}

export function referencesCases(suiteName: string, cases: readonly ReferencesCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "references");
                const position = new vscode.Position(line, character);
                const raw = await waitFor(
                    () =>
                        vscode.commands.executeCommand(
                            "_executeReferenceProvider",
                            document.uri,
                            position,
                            { includeDeclaration: spec.includeDeclaration ?? true },
                        ) as Promise<LocationLike[] | undefined>,
                    (value) => value !== undefined,
                    6_000,
                ).catch(() => undefined);
                // The internal reference command always asks the server for the
                // declaration included, and its `{ includeDeclaration }` toggle
                // is ignored, so a `false` case cannot be honored through it.
                // Honoring the spec means removing the reference that lands on
                // the symbol's declaration. The fixtures are single-file and the
                // first occurrence of the word is the declaration site: the
                // cursor is always inside the identifier (on the declaration
                // itself for the "unused" fixture), so the word the cursor sits
                // on identifies it.
                let effective: unknown = raw;
                if (spec.includeDeclaration === false) {
                    const matches = Array.isArray(raw) ? (raw as LocationLike[]) : raw ? [raw as LocationLike] : [];
                    const { content: fixtureContent, line: fixtureLine, character: fixtureCharacter } = fixtureWithCursor(spec.source);
                    const word = identifierAtPosition(fixtureContent, fixtureLine, fixtureCharacter);
                    const declaration = word ? firstOccurrenceRange(fixtureContent, word) : undefined;
                    if (declaration) {
                        effective = matches.filter((location) => {
                            const range = normalizeRange(location.range);
                            if (!range) return true;
                            return !(range.start.line === declaration.start.line
                                && range.start.character === declaration.start.character
                                && range.end.line === declaration.end.line
                                && range.end.character === declaration.end.character);
                        });
                    }
                }
                const lines = targetLines(effective);
                if (spec.expectNone) {
                    assert.equal(lines.length, 0, `expected no references; got lines: ${lines.join(",")}`);
                    return;
                }
                if (spec.expectCount !== undefined) {
                    assert.equal(lines.length, spec.expectCount, `expected ${spec.expectCount} reference(s); got ${lines.length}`);
                }
                for (const wanted of spec.expectSomeLines ?? []) {
                    assert.ok(lines.includes(wanted), `expected a reference on line ${wanted}; got ${lines.join(",") || "none"}`);
                }
                for (const wanted of spec.expectAllLines ?? []) {
                    assert.ok(lines.includes(wanted), `expected a reference on line ${wanted}; got ${lines.join(",") || "none"}`);
                }
            });
        }
    });
}

export interface RenameCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly newName: string;
    /** Exact number of edits expected across the workspace. */
    readonly expectEditCount?: number;
    /** The returned workspace edit must be absent or empty. */
    readonly expectFailure?: boolean;
}

export function renameCases(suiteName: string, cases: readonly RenameCase[]): void {
    type TextEditLike = { readonly range?: unknown; readonly newText?: unknown };
    const isTextEditLike = (value: unknown): value is TextEditLike =>
        typeof value === "object" && value !== null && ("newText" in value || "range" in value);
    const textEditNewText = (edit: TextEditLike): string =>
        typeof edit.newText === "string" ? edit.newText : "";
    // The internal rename command does not always return a WorkspaceEdit the
    // harness can iterate with entries(); collect the edits from whatever
    // shape it does return, tolerating absent/empty results.
    // The internal rename command returns `{ edits: [{ resource, textEdit }] }`
    // whose items wrap the TextEdit in a `textEdit` property (text stored as
    // `text`, not `newText`); unwrap those into the plain {range, newText}
    // shape the rest of the driver expects.
    const unwrapRenameEdits = (items: unknown[]): TextEditLike[] => {
        const out: TextEditLike[] = [];
        for (const item of items) {
            if (!item || typeof item !== "object") {
                continue;
            }
            const wrapper = item as { resource?: unknown; textEdit?: unknown };
            if (wrapper.textEdit && typeof wrapper.textEdit === "object") {
                const inner = wrapper.textEdit as { range?: unknown; text?: unknown };
                if (typeof inner.text === "string") {
                    out.push({ range: inner.range, newText: inner.text });
                }
                continue;
            }
            if (isTextEditLike(item)) {
                out.push(item);
            }
        }
        return out;
    };
    const collectRenameEdits = (value: unknown): TextEditLike[] => {
        if (!value || typeof value !== "object") {
            return [];
        }
        const anyEdit = value as { entries?: unknown; changes?: unknown; edits?: unknown };
        if (typeof anyEdit.entries === "function") {
            const collected: TextEditLike[] = [];
            // Call entries() with the edit object as receiver so prototype
            // methods that rely on `this` keep working.
            for (const pair of (anyEdit.entries as (this: unknown) => Iterable<unknown>).call(anyEdit)) {
                const elements = Array.isArray(pair) ? pair : [pair];
                for (const element of elements) {
                    if (Array.isArray(element)) {
                        collected.push(...element.filter(isTextEditLike));
                    } else if (isTextEditLike(element)) {
                        collected.push(element);
                    }
                }
            }
            return collected;
        }
        if (anyEdit.changes !== undefined) {
            if (Array.isArray(anyEdit.changes)) {
                return unwrapRenameEdits(anyEdit.changes);
            }
            if (typeof anyEdit.changes === "object" && anyEdit.changes !== null) {
                const collected: TextEditLike[] = [];
                for (const list of Object.values(anyEdit.changes as Record<string, unknown>)) {
                    if (Array.isArray(list)) {
                        collected.push(...unwrapRenameEdits(list));
                    }
                }
                return collected;
            }
            return [];
        }
        if (Array.isArray(anyEdit.edits)) {
            return unwrapRenameEdits(anyEdit.edits);
        }
        return [];
    };
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "rename");
                const position = new vscode.Position(line, character);
                const edit = await vscode.commands
                    .executeCommand<vscode.WorkspaceEdit>(
                        "_executeDocumentRenameProvider",
                        document.uri,
                        position,
                        spec.newName,
                    )
                    .then((value) => value, () => undefined);
                // For cases that require rejection, the provider must not
                // produce edits at all. The command returns either `undefined`
                // or `{ rejectReason, edits: [] }` on failure, so collect
                // first and require an empty edit set.
                if (spec.expectFailure) {
                    const edits = collectRenameEdits(edit);
                    assert.equal(edits.length, 0, `expected rename to be rejected; got ${edits.length} edit(s)`);
                    return;
                }
                // The command returns `{ rejectReason, edits: [] }` on failure
                // instead of a WorkspaceEdit; surface the server's reason first.
                const rejectReason = (edit as { rejectReason?: string } | undefined)?.rejectReason;
                if (rejectReason) {
                    throw new Error(`rename rejected: ${String(rejectReason)}`);
                }
                assert.ok(edit, "expected a WorkspaceEdit from the rename provider");
                let count = 0;
                let renamed = 0;
                for (const entry of collectRenameEdits(edit)) {
                    count += 1;
                    if (textEditNewText(entry) === spec.newName) {
                        renamed += 1;
                    }
                }
                assert.ok(count > 0, "rename produced no edits");
                assert.equal(renamed, count, "rename edit replaced text with something else than the new name");
                if (spec.expectEditCount !== undefined) {
                    assert.equal(count, spec.expectEditCount, `expected ${spec.expectEditCount} edit(s); got ${count}`);
                }
            });
        }
    });
}

export interface SignatureHelpCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** Substrings drawn from the flattened signature labels. */
    readonly contains?: readonly string[];
    /** Substrings drawn from the flattened documentation (signature + parameter docs). */
    readonly expectDocumentation?: readonly string[];
    /** The 0-based active parameter index. */
    readonly expectActiveParameter?: number;
    readonly expectNone?: boolean;
}

function flattenSignatures(help: vscode.SignatureHelp): string {
    return help.signatures.map((signature) => signature.label).join("\n");
}

function flattenDocumentation(help: vscode.SignatureHelp): string {
    const parts: string[] = [];
    const pushText = (value: unknown): void => {
        if (typeof value === "string") {
            parts.push(value);
            return;
        }
        if (value && typeof value === "object" && "value" in value) {
            const inner = (value as { value: unknown }).value;
            if (typeof inner === "string") {
                parts.push(inner);
            }
            return;
        }
        if (value instanceof vscode.MarkdownString) {
            parts.push(value.value);
        }
    };
    for (const signature of help.signatures) {
        pushText(signature.documentation);
        for (const parameter of signature.parameters) {
            pushText(parameter.documentation);
        }
    }
    return parts.join("\n");
}

export function signatureHelpCases(suiteName: string, cases: readonly SignatureHelpCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "signature-help");
                const position = new vscode.Position(line, character);
                const help = await waitFor(
                    () =>
                        vscode.commands.executeCommand(
                            "_executeSignatureHelpProvider",
                            document.uri,
                            position,
                        ) as Promise<vscode.SignatureHelp | undefined>,
                    (value) => value !== undefined && value.signatures.length > 0,
                    6_000,
                ).catch(() => undefined);
                if (spec.expectNone) {
                    assert.ok(
                        !help || help.signatures.length === 0,
                        `expected no signature help; got: ${help ? flattenSignatures(help) : "none"}`,
                    );
                    return;
                }
                assert.ok(help, "expected signature help");
                const text = flattenSignatures(help);
                for (const chunk of spec.contains ?? []) {
                    assert.ok(text.includes(chunk), `expected ${JSON.stringify(chunk)} in ${text}`);
                }
                const docs = flattenDocumentation(help);
                for (const chunk of spec.expectDocumentation ?? []) {
                    assert.ok(docs.includes(chunk), `expected ${JSON.stringify(chunk)} in ${docs || "(no documentation)"}`);
                }
                if (spec.expectActiveParameter !== undefined) {
                    assert.equal(help.activeParameter, spec.expectActiveParameter, `active parameter for: ${text}`);
                }
            });
        }
    });
}

export interface FormattingCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** The expected document text after applying the returned edits. */
    readonly expectText?: string;
    /** When true, no edits must be returned. */
    readonly expectNoEdits?: boolean;
    readonly expectEditCount?: number;
}

function applyTextEdits(text: string, edits: readonly vscode.TextEdit[]): string {
    const offsets: Array<{ start: number; end: number; text: string }> = [];
    for (const edit of edits) {
        const range = normalizeRange(edit.range);
        if (!range) {
            continue;
        }
        // The internal `_executeFormatDocumentProvider` command answers with
        // the editor's own edit shape: the replacement is in `text`, not in
        // `newText` (the LSP spelling).
        const replacement = (edit as { text?: string }).text ?? edit.newText;
        offsets.push({
            start: offsetOf(text, range.start),
            end: offsetOf(text, range.end),
            text: replacement,
        });
    }
    offsets.sort((a, b) => b.start - a.start);
    let result = text;
    for (const edit of offsets) {
        result = result.slice(0, edit.start) + edit.text + result.slice(edit.end);
    }
    return result;
}

function offsetOf(text: string, position: { line: number; character: number }): number {
    let offset = 0;
    let line = 0;
    while (line < position.line) {
        const next = text.indexOf("\n", offset);
        if (next < 0) {
            return text.length;
        }
        offset = next + 1;
        line += 1;
    }
    return Math.min(offset + position.character, text.length);
}

export function formattingCases(suiteName: string, cases: readonly FormattingCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const document = await openCaseDocument(spec, "formatting");
                const edits = await vscode.commands
                    .executeCommand<vscode.TextEdit[]>(
                        "_executeFormatDocumentProvider",
                        document.uri,
                        { tabSize: 4, insertSpaces: true },
                    )
                    .then((value) => value, () => undefined);
                if (spec.expectNoEdits) {
                    assert.ok(!edits || edits.length === 0, `expected no edits; got ${edits?.length}`);
                    return;
                }
                if (spec.expectEditCount !== undefined) {
                    assert.ok(edits, "expected edits");
                    assert.equal(edits.length, spec.expectEditCount, `expected ${spec.expectEditCount} edit(s)`);
                }
                if (spec.expectText !== undefined) {
                    const finalText = edits && edits.length > 0 ? applyTextEdits(spec.source, edits) : spec.source;
                    assert.equal(finalText, spec.expectText);
                }
            });
        }
    });
}

export interface CodeActionCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** Substrings the action titles must contain. */
    readonly expectTitle?: readonly string[];
    /** Kind of actions to request (optional filter). */
    readonly kind?: vscode.CodeActionKind;
    /** How long to wait for a positive expectation; diagnostics may take a moment. */
    readonly timeoutMs?: number;
    readonly expectNone?: boolean;
}

export function codeActionCases(suiteName: string, cases: readonly CodeActionCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const hasMarker = spec.source.includes("__CURSOR__");
                const { line, character } = hasMarker
                    ? fixtureWithCursor(spec.source)
                    : { line: 0, character: 0 };
                const document = await openCaseDocument(spec, "code-actions");
                const range = new vscode.Range(line, character, line, character);
                const filter: { kind?: vscode.CodeActionKind } = {};
                if (spec.kind) {
                    filter.kind = spec.kind;
                }
                const fetchActions = () =>
                    vscode.commands.executeCommand(
                        "_executeCodeActionProvider",
                        document.uri,
                        range,
                        filter,
                    ) as Promise<Array<vscode.CodeAction | vscode.Command> | undefined>;
                const titleOf = (action: vscode.CodeAction | vscode.Command) =>
                    "title" in action ? String(action.title) : String((action as vscode.Command).title);
                // The test host ships the built-in Copilot Chat extension,
                // which injects AI actions ("Fix", "Explain", "Generate") into
                // every diagnostic. They are not ours, are not reproducible,
                // and drown every "no action expected" case, so they are
                // filtered out everywhere the suite reads code actions.
                const isCopilotNoise = (action: vscode.CodeAction | vscode.Command) => {
                    const kind = typeof (action as unknown as { kind?: unknown }).kind === "string"
                        ? (action as unknown as { kind: string }).kind
                        : "";
                    return (action as unknown as { isAI?: unknown }).isAI === true || kind.startsWith("quickfix.copilot") ||
                           kind.startsWith("quickfix.explain.copilot") || kind.startsWith("quickfix.generate.copilot");
                };
                const dropNoise = (list: Array<vscode.CodeAction | vscode.Command> | undefined) =>
                    (list ?? []).filter((action) => !isCopilotNoise(action));
                // The server publishes diagnostics on its own loop; a provider
                // asked before they arrive answers an empty list, which would
                // fail the case for a timing reason instead of the behaviour
                // it checks. Wait for the expected title to show up.
                // The server may need several seconds to check the first
                // document of a fresh workspace, and diagnostics are what the
                // provider reads; give the poll enough room (the diagnostics
                // driver waits 12s for the same reason).
                const timeoutMs = spec.timeoutMs ?? 15_000;
                const expected = spec.expectTitle ?? [];
                const actions = await waitFor(
                    fetchActions,
                    (value) =>
                        expected.length === 0 ||
                        (Array.isArray(value) &&
                            expected.every((chunk) =>
                                dropNoise(value).some((action) => titleOf(action).includes(chunk)),
                            )),
                    timeoutMs,
                ).catch(() => undefined);
                const titles = dropNoise(actions).map(titleOf);
                if (spec.expectNone) {
                    // Let the diagnostics settle before accepting an empty answer.
                    await new Promise((resolve) => setTimeout(resolve, 1_500));
                    const late = await fetchActions().catch(() => undefined);
                    const lateTitles = dropNoise(late).map(titleOf);
                    assert.equal(lateTitles.length, 0, `expected no actions; got: ${lateTitles.join(", ")}`);
                    return;
                }
                for (const chunk of spec.expectTitle ?? []) {
                    assert.ok(
                        titles.some((title) => title.includes(chunk)),
                        `expected an action containing ${JSON.stringify(chunk)}; got: ${titles.join(", ") || "none"}`,
                    );
                }
            });
        }
    });
}

export interface DiagnosticExpectation {
    readonly code?: string;
    readonly contains?: string;
    /** 1-based line (as the compiler prints it). */
    readonly line?: number;
    /** 1-based column (as the compiler prints it). */
    readonly col?: number;
    readonly severity?: "error" | "warning" | "info" | "hint";
}

export interface DiagnosticCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly expect: readonly DiagnosticExpectation[];
    /** When true, the file must produce zero diagnostics (it is valid cp3). */
    readonly expectNoDiagnostics?: boolean;
    /** When true, the file must produce no error-severity diagnostics (warnings are fine). */
    readonly expectNoErrors?: boolean;
}

function severityOf(diagnostic: vscode.Diagnostic): string {
    switch (diagnostic.severity) {
        case vscode.DiagnosticSeverity.Warning:
            return "warning";
        case vscode.DiagnosticSeverity.Information:
            return "info";
        case vscode.DiagnosticSeverity.Hint:
            return "hint";
        default:
            return "error";
    }
}

export async function waitForDiagnostics(
    uri: vscode.Uri,
    timeoutMs = 8_000,
): Promise<vscode.Diagnostic[]> {
    return waitFor(
        async () => vscode.languages.getDiagnostics(uri),
        (diagnostics) => diagnostics !== undefined,
        timeoutMs,
        400,
    );
}

/** Matches a diagnostic code against the frozen wildcard grammar: runs of 2+ `x`/`?` mean "any run", a lone `x`/`?` is one character, `*` is any run. */
export function codeMatches(actual: string, pattern: string): boolean {
    const actualText = actual.toLowerCase();
    const normalized = pattern.replace(/[xX?]{2,}/g, "*").toLowerCase();
    let a = 0;
    let p = 0;
    let starA = -1;
    let starP = -1;
    while (a < actualText.length) {
        if (p < normalized.length && (normalized[p] === "x" || normalized[p] === "?" || normalized[p] === actualText[a])) {
            a += 1;
            p += 1;
        } else if (p < normalized.length && normalized[p] === "*") {
            starP = p;
            starA = a;
            p += 1;
        } else if (starP !== -1) {
            p = starP + 1;
            starA += 1;
            a = starA;
        } else {
            return false;
        }
    }
    while (p < normalized.length && normalized[p] === "*") {
        p += 1;
    }
    return p === normalized.length;
}

function diagnosticsMatch(diagnostic: vscode.Diagnostic, expectation: DiagnosticExpectation): boolean {
    if (expectation.severity && severityOf(diagnostic) !== expectation.severity) {
        return false;
    }
    if (expectation.code) {
        const code = typeof diagnostic.code === "object" ? String(diagnostic.code.value) : String(diagnostic.code ?? "");
        if (!codeMatches(code, expectation.code)) {
            return false;
        }
    }
    if (expectation.contains && !diagnostic.message.includes(expectation.contains)) {
        return false;
    }
    if (expectation.line !== undefined && diagnostic.range.start.line + 1 !== expectation.line) {
        return false;
    }
    if (expectation.col !== undefined && diagnostic.range.start.character + 1 !== expectation.col) {
        return false;
    }
    return true;
}

export function diagnosticCases(suiteName: string, cases: readonly DiagnosticCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const document = await openCaseDocument(spec, "diagnostics");
                // The server coalesces rapid changes, so the diagnostics of a
                // freshly opened file arrive a moment later. A case that
                // expects diagnostics waits for them; a case that expects
                // none waits for the list to settle.
                const matches = (list: readonly vscode.Diagnostic[]): boolean =>
                    spec.expect.every((expectation) =>
                        list.some((diagnostic) => diagnosticsMatch(diagnostic, expectation)));
                let diagnostics = vscode.languages.getDiagnostics(document.uri);
                if (spec.expectNoDiagnostics || spec.expectNoErrors) {
                    const deadline = Date.now() + 12_000;
                    let stablePolls = 0;
                    while (Date.now() < deadline && stablePolls < 3) {
                        await new Promise((resolve) => setTimeout(resolve, 400));
                        const fresh = vscode.languages.getDiagnostics(document.uri);
                        stablePolls = fresh.length === diagnostics.length ? stablePolls + 1 : 0;
                        diagnostics = fresh;
                    }
                } else {
                    const deadline = Date.now() + 8_000;
                    while (Date.now() < deadline && !matches(diagnostics)) {
                        await new Promise((resolve) => setTimeout(resolve, 300));
                        diagnostics = vscode.languages.getDiagnostics(document.uri);
                    }
                }
                if (spec.expectNoDiagnostics) {
                    assert.equal(
                        diagnostics.length,
                        0,
                        `expected no diagnostics; got: ${diagnostics
                            .map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${String(d.code ?? "")} ${d.message}`)
                            .join(" | ")}`,
                    );
                    return;
                }
                if (spec.expectNoErrors) {
                    const errors = diagnostics.filter((d) => severityOf(d) === "error");
                    assert.equal(
                        errors.length,
                        0,
                        `expected no errors; got: ${errors
                            .map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${String(d.code ?? "")} ${d.message}`)
                            .join(" | ")}`,
                    );
                    return;
                }
                for (const expectation of spec.expect) {
                    const found = diagnostics.some((diagnostic) => diagnosticsMatch(diagnostic, expectation));
                    assert.ok(
                        found,
                        `no diagnostic matched ${JSON.stringify(expectation)}; got: ${diagnostics
                            .map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${severityOf(d)} ${String(d.code ?? "")} ${d.message}`)
                            .join(" | ") || "none"}`,
                    );
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Symbols, folding, hierarchy
// ---------------------------------------------------------------------------

function symbolNames(symbols: readonly unknown[]): string[] {
    const names: string[] = [];
    for (const symbol of symbols) {
        const anySymbol = symbol as { name?: string; children?: unknown[] };
        if (typeof anySymbol?.name === "string") {
            names.push(anySymbol.name);
        }
        if (Array.isArray(anySymbol?.children)) {
            names.push(...symbolNames(anySymbol.children));
        }
    }
    return names;
}

export interface SymbolCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** Names that must appear among the symbols (recursive). */
    readonly contains?: readonly string[];
    readonly expectCount?: number;
}

export function documentSymbolCases(suiteName: string, cases: readonly SymbolCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const document = await openCaseDocument(spec, "symbols");
                const raw = await waitFor(
                    () =>
                        vscode.commands.executeCommand(
                            "_executeDocumentSymbolProvider",
                            document.uri,
                        ) as Promise<unknown[] | undefined>,
                    (value) => value !== undefined,
                    6_000,
                ).catch(() => undefined);
                const names = symbolNames(raw ?? []);
                for (const chunk of spec.contains ?? []) {
                    assert.ok(names.includes(chunk), `expected ${JSON.stringify(chunk)} in symbols; got: ${names.join(", ") || "none"}`);
                }
                if (spec.expectCount !== undefined) {
                    assert.equal(names.length, spec.expectCount, `expected ${spec.expectCount} symbol(s); got ${names.length}: ${names.join(", ")}`);
                }
            });
        }
    });
}

export function workspaceSymbolCases(
    suiteName: string,
    cases: readonly (SymbolCase & { readonly query: string })[],
): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                await openCaseDocument(spec, "symbols");
                const raw = await waitFor(
                    () =>
                        vscode.commands.executeCommand(
                            "_executeWorkspaceSymbolProvider",
                            spec.query,
                        ) as Promise<unknown[] | undefined>,
                    (value) => value !== undefined,
                    6_000,
                ).catch(() => undefined);
                const names = symbolNames(raw ?? []);
                for (const chunk of spec.contains ?? []) {
                    assert.ok(names.includes(chunk), `expected ${JSON.stringify(chunk)} in workspace symbols; got: ${names.join(", ") || "none"}`);
                }
            });
        }
    });
}

export interface FoldingRangeCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly expectCount?: number;
    readonly expectAtLeast?: number;
}

export function foldingRangeCases(suiteName: string, cases: readonly FoldingRangeCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const document = await openCaseDocument(spec, "folding");
                const ranges = await vscode.commands
                    .executeCommand<vscode.FoldingRange[]>("_executeFoldingRangeProvider", document.uri)
                    .then((value) => value, () => undefined);
                const count = ranges?.length ?? 0;
                if (spec.expectCount !== undefined) {
                    assert.equal(count, spec.expectCount, `expected ${spec.expectCount} folding range(s)`);
                }
                if (spec.expectAtLeast !== undefined) {
                    assert.ok(count >= spec.expectAtLeast, `expected at least ${spec.expectAtLeast} folding range(s); got ${count}`);
                }
            });
        }
    });
}

export interface SelectionRangeCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** Number of selection ranges VS Code requests (all nest). */
    readonly expectRanges?: number;
}

export function selectionRangeCases(suiteName: string, cases: readonly SelectionRangeCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "selection");
                const position = new vscode.Position(line, character);
                const results = await vscode.commands
                    .executeCommand<vscode.SelectionRange[]>(
                        "_executeSelectionRangeProvider",
                        document.uri,
                        [position],
                    )
                    .then((value) => value, () => undefined);
                // The nesting the tests care about is the sequence of
                // semantic expansion steps, so the pieces VS Code always
                // merges in are discounted: the full line (column 1 to the
                // line's max column, no newline), the whole-model range,
                // and exact duplicates of a level (an expansion that lands
                // on the trimmed line shows up both as the level and as
                // the synthetic the command inserts between layers).
                const key = (r: { startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }): string =>
                    `${r.startLineNumber}:${r.startColumn}-${r.endLineNumber}:${r.endColumn}`;
                const isFullLine = (entry: { startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }): boolean =>
                    entry.startLineNumber === entry.endLineNumber &&
                    entry.startColumn === 1 &&
                    entry.endColumn === document.lineAt(entry.startLineNumber - 1).text.length + 1;
                const isTrimmedLine = (entry: { startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }): boolean => {
                    if (entry.startLineNumber !== entry.endLineNumber) return false;
                    const raw = document.lineAt(entry.startLineNumber - 1).text;
                    if (raw.trim().length === 0) return false;
                    const first = raw.search(/\S/) + 1;
                    const last = raw.replace(/\s+$/, "").length + 1;
                    return entry.startColumn === first && entry.endColumn === last;
                };
                const isWholeModel = (entry: { startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }): boolean =>
                    entry.startLineNumber === 1 &&
                    entry.startColumn === 1 &&
                    entry.endLineNumber === document.lineCount &&
                    entry.endColumn === 1;
                try {
                    const flat = (Array.isArray(results) ? results : []).flat() as unknown as Array<{ startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }>;
                    const seen = new Set<string>();
                    const levels = flat.filter(
                        (entry) =>
                            entry &&
                            typeof entry.startLineNumber === "number" &&
                            !isFullLine(entry) &&
                            !isTrimmedLine(entry) &&
                            !isWholeModel(entry) &&
                            !seen.has(key(entry)) &&
                            (seen.add(key(entry)), true),
                    );
                    var count = levels.length;
                } catch {
                    // A malformed answer counts as no levels rather than a crash.
                    var count = 0;
                }
                if (spec.expectRanges !== undefined) {
                    assert.equal(count, spec.expectRanges, `expected ${spec.expectRanges} nested selection range(s)`);
                } else {
                    assert.ok(count > 0, "expected at least one nested selection range");
                }
            });
        }
    });
}

export interface HierarchyCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly prepareContains?: readonly string[];
    readonly supertypesContains?: readonly string[];
    readonly subtypesContains?: readonly string[];
    readonly incomingContains?: readonly string[];
    readonly outgoingContains?: readonly string[];
}

interface ItemLike {
    readonly name?: string;
    readonly label?: string;
}

function itemNames(items: readonly ItemLike[] | undefined): string[] {
    return (items ?? []).map((item) => item.name ?? item.label ?? "");
}

export function typeHierarchyCases(suiteName: string, cases: readonly HierarchyCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "type-hierarchy");
                const position = new vscode.Position(line, character);
                const roots = await vscode.commands
                    .executeCommand<ItemLike[]>("_executePrepareTypeHierarchy", document.uri, position)
                    .then((value) => value, () => undefined);
                const rootNames = itemNames(roots);
                for (const chunk of spec.prepareContains ?? []) {
                    assert.ok(rootNames.includes(chunk), `expected ${JSON.stringify(chunk)} in prepared type hierarchy; got: ${rootNames.join(", ") || "none"}`);
                }
                const root = roots?.[0];
                if (spec.supertypesContains) {
                    const supertypes = await vscode.commands
                        .executeCommand<ItemLike[]>("_executeProvideSupertypes", root)
                        .then((value) => value, () => undefined);
                    const names = itemNames(supertypes);
                    for (const chunk of spec.supertypesContains) {
                        assert.ok(names.includes(chunk), `expected supertype ${JSON.stringify(chunk)}; got: ${names.join(", ") || "none"}`);
                    }
                }
                if (spec.subtypesContains) {
                    const subtypes = await vscode.commands
                        .executeCommand<ItemLike[]>("_executeProvideSubtypes", root)
                        .then((value) => value, () => undefined);
                    const names = itemNames(subtypes);
                    for (const chunk of spec.subtypesContains) {
                        assert.ok(names.includes(chunk), `expected subtype ${JSON.stringify(chunk)}; got: ${names.join(", ") || "none"}`);
                    }
                }
            });
        }
    });
}

export function callHierarchyCases(suiteName: string, cases: readonly HierarchyCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = fixtureWithCursor(spec.source);
                const document = await openCaseDocument(spec, "call-hierarchy");
                const position = new vscode.Position(line, character);
                const roots = await vscode.commands
                    .executeCommand<ItemLike[]>("_executePrepareCallHierarchy", document.uri, position)
                    .then((value) => value, () => undefined);
                const rootNames = itemNames(roots);
                for (const chunk of spec.prepareContains ?? []) {
                    assert.ok(rootNames.includes(chunk), `expected ${JSON.stringify(chunk)} in prepared call hierarchy; got: ${rootNames.join(", ") || "none"}`);
                }
                const root = roots?.[0];
                if (spec.incomingContains) {
                    const calls = await vscode.commands
                        .executeCommand<Array<{ from: ItemLike }>>("_executeProvideIncomingCalls", root)
                        .then((value) => value, () => undefined);
                    const names = itemNames(calls?.map((call) => call.from));
                    for (const chunk of spec.incomingContains) {
                        assert.ok(names.includes(chunk), `expected incoming call from ${JSON.stringify(chunk)}; got: ${names.join(", ") || "none"}`);
                    }
                }
                if (spec.outgoingContains) {
                    const calls = await vscode.commands
                        .executeCommand<Array<{ to: ItemLike }>>("_executeProvideOutgoingCalls", root)
                        .then((value) => value, () => undefined);
                    const names = itemNames(calls?.map((call) => call.to));
                    for (const chunk of spec.outgoingContains) {
                        assert.ok(names.includes(chunk), `expected outgoing call to ${JSON.stringify(chunk)}; got: ${names.join(", ") || "none"}`);
                    }
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Robustness: single probes against pathological input
// ---------------------------------------------------------------------------

export type RobustnessProbe = "completion" | "hover" | "definition" | "references" | "formatting" | "codeAction";

export interface RobustnessCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    readonly probe: RobustnessProbe;
}

async function probeOnce(document: vscode.TextDocument, position: vscode.Position, probe: RobustnessProbe): Promise<void> {
    const uri = document.uri;
    switch (probe) {
        case "completion":
            await vscode.commands.executeCommand("_executeCompletionItemProvider", uri, position);
            return;
        case "hover":
            await vscode.commands.executeCommand("_executeHoverProvider", uri, position);
            return;
        case "definition":
            await vscode.commands.executeCommand("_executeDefinitionProvider", uri, position);
            return;
        case "references":
            await vscode.commands.executeCommand("_executeReferenceProvider", uri, position, { includeDeclaration: true });
            return;
        case "formatting":
            await vscode.commands.executeCommand("_executeFormatDocumentProvider", uri, { tabSize: 4, insertSpaces: true });
            return;
        case "codeAction": {
            const range = new vscode.Range(position, position);
            await vscode.commands.executeCommand("_executeCodeActionProvider", uri, range, {});
            return;
        }
    }
}

/**
 * Every probe must answer without an unhandled exception or a protocol
 * failure; the answer itself may be anything (including null).
 */
export function robustnessCases(suiteName: string, cases: readonly RobustnessCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const { line, character } = spec.source.includes("__CURSOR__")
                    ? fixtureWithCursor(spec.source)
                    : { line: 0, character: 0 };
                const document = await openCaseDocument(spec, "edge-cases");
                const position = new vscode.Position(line, character);
                try {
                    await probeOnce(document, position, spec.probe);
                } catch (error) {
                    throw new Error(`${spec.probe} threw on ${JSON.stringify(spec.name)}: ${String(error)}`);
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Auto-import (extension workspace index + server std auto-imports)
// ---------------------------------------------------------------------------

export interface AutoImportCase extends CaseModules {
    readonly name: string;
    readonly source: string;
    /** The completion item that must be offered (its label). */
    readonly expectOffered?: string;
    /** Substring of an additionalTextEdit that must add an import line. */
    readonly expectImportInserted?: string;
    /** True when the item must not appear (or must carry no edits). */
    readonly expectNoItem?: boolean;
    readonly timeoutMs?: number;
}

interface AnyCompletionItem {
    label?: unknown;
    kind?: unknown;
    insertText?: unknown;
    textEdit?: { newText?: string; text?: string };
    additionalTextEdits?: readonly { range: vscode.Range; newText?: string; text?: string }[];
    command?: { title?: string };
}

/** The internal completion command resolves `{ incomplete, suggestions }` rather
 *  than the public CompletionList `{ isIncomplete, items }` shape; read it
 *  defensively (the items fallback is kept in case VS Code ever returns the
 *  public shape). */
interface InternalCompletionList {
    readonly incomplete?: boolean;
    readonly suggestions?: readonly AnyCompletionItem[];
    readonly items?: readonly AnyCompletionItem[];
}

function internalCompletionItems(value: InternalCompletionList | undefined): readonly AnyCompletionItem[] {
    // The built-in snippet provider contributes the extension's snippet
    // templates to every completion list; they are a template catalog, not
    // language intelligence, and no expectation here is about them, so they
    // are dropped at the boundary (same treatment as Copilot AI noise).
    const kindOf = (item: AnyCompletionItem): number | string | undefined => {
        const kind = (item as { kind?: unknown }).kind;
        return typeof kind === "number" || typeof kind === "string" ? kind : undefined;
    };
    return ((value?.suggestions ?? value?.items ?? []) as readonly AnyCompletionItem[]).filter((item) => {
        const kind = kindOf(item);
        // The internal command returns raw items: the built-in snippet
        // provider marks its templates with kind 28 and object labels
        // ({ label, description }). Everything else (the server, and the
        // extension's own items, which also happen to use kind 28) carries
        // a plain string label, so only the object shape identifies them.
        return !(kind === 28 && typeof (item as { label?: unknown }).label !== "string");
    });
}

export function autoImportCases(suiteName: string, cases: readonly AutoImportCase[]): void {
    // The edit text a completion item carries: the additional edits (the
    // import line) plus the item's own text edit. The internal command
    // answers with the editor's shape, where the replacement is in `text`.
    const importEditText = (item: AnyCompletionItem): string =>
        [
            ...(item.additionalTextEdits ?? []).map((edit) => edit.text ?? edit.newText ?? ""),
            item.textEdit?.text ?? item.textEdit?.newText ?? String(item.insertText ?? ""),
        ].join("\\n");
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                // The extension's auto-import provider only offers completions
                // when a prefix is already typed; a bare cursor returns an
                // empty list by design. So when the case pins an offered name,
                // extend any bare __CURSOR__ with a partial of that name.
                let source = spec.source;
                if (spec.expectOffered && source.includes("__CURSOR__")) {
                    const partial = spec.expectOffered.slice(
                        0,
                        Math.max(1, Math.floor(spec.expectOffered.length / 2)),
                    );
                    source = source.split("__CURSOR__").join(partial + "__CURSOR__");
                }
                const { line, character } = fixtureWithCursor(source);
                const document = await openCaseDocument({ ...spec, source }, "auto-import");
                const position = new vscode.Position(line, character);
                const labelOf = (item: AnyCompletionItem): string =>
                    typeof item.label === "string"
                        ? item.label
                        : String((item.label as { label?: unknown } | undefined)?.label ?? "");
                // The internal command resolves `{ incomplete, suggestions }` (not
                // the public CompletionList shape), so read it defensively and
                // apply the same current-word prefix filter the editor applies
                // client-side; every poll then sees exactly what the user sees.
                const linePrefix = (() => {
                    const lineText = document.lineAt(position.line).text.slice(0, position.character);
                    return (lineText.match(/[A-Za-z_$][\w$]*$/)?.[0] ?? "").toLowerCase();
                })();
                const requestList = async (): Promise<readonly AnyCompletionItem[] | undefined> => {
                    const value = await vscode.commands
                        .executeCommand<InternalCompletionList>(
                            "_executeCompletionItemProvider",
                            document.uri,
                            position,
                        )
                        .then((result) => result, () => undefined);
                    if (value === undefined) {
                        return undefined;
                    }
                    const items = internalCompletionItems(value);
                    return linePrefix
                        ? items.filter((item) => labelOf(item).toLowerCase().startsWith(linePrefix))
                        : items;
                };
                // For positive cases, poll until the offered item shows up
                // (the workspace index rebuilds on file create).
                let items: readonly AnyCompletionItem[] | undefined;
                if (spec.expectOffered && !spec.expectNoItem) {
                    // The expected import text is part of the expectation: a
                    // stale index can offer the right name with the wrong
                    // module, so the poll waits for the edit that matches.
                    // Several items may carry the expected label (a builtin
                    // and a project module share one), so any of them with
                    // the right edit satisfies the case.
                    const wantedImport = spec.expectImportInserted;
                    const matches = (value: readonly AnyCompletionItem[]): boolean => {
                        const candidates = value.filter((candidate) => labelOf(candidate) === spec.expectOffered);
                        if (candidates.length === 0) return false;
                        if (!wantedImport) return true;
                        return candidates.some((candidate) => importEditText(candidate).includes(wantedImport));
                    };
                    items = await waitFor(
                        requestList,
                        matches,
                        spec.timeoutMs ?? 10_000,
                    ).catch(() => undefined);
                } else {
                    // Negative cases: let the index settle, then use the last
                    // response so a stale first answer does not pass by accident.
                    const deadline = Date.now() + (spec.timeoutMs ?? 10_000);
                    while (Date.now() < deadline) {
                        items = await requestList();
                        if (items) {
                            break;
                        }
                        await new Promise((resolve) => setTimeout(resolve, 400));
                    }
                    await new Promise((resolve) => setTimeout(resolve, 1_500));
                    items = (await requestList()) ?? items;
                }
                const offeredItems = items ?? [];
                const labels = offeredItems.map(labelOf);
                if (spec.expectNoItem) {
                    // The name may still be offered as an ordinary identifier
                    // (it is in scope); what must not happen is an import
                    // edit for it.
                    const item = offeredItems.find((candidate) => labelOf(candidate) === spec.expectOffered);
                    const carriesImport = (item?.additionalTextEdits ?? []).some((edit) =>
                        (edit.text ?? edit.newText ?? "").includes("import"),
                    );
                    assert.ok(
                        !carriesImport,
                        `expected ${JSON.stringify(spec.expectOffered)} to not be offered as an import; got: ${labels.join(", ")}`,
                    );
                    return;
                }
                assert.ok(spec.expectOffered, "auto-import case needs expectOffered");
                const wantedImport = spec.expectImportInserted;
                const candidates = offeredItems.filter((item) => labelOf(item) === spec.expectOffered);
                const offered = wantedImport
                    ? candidates.find((item) => importEditText(item).includes(wantedImport))
                    : candidates[0];
                assert.ok(offered, `expected ${JSON.stringify(spec.expectOffered)} to be offered; got: ${labels.join(", ") || "none"}`);
                if (spec.expectImportInserted) {
                    const editsText = importEditText(offered);
                    assert.ok(
                        editsText.includes(spec.expectImportInserted),
                        `expected an import edit containing ${JSON.stringify(spec.expectImportInserted)}; got edits: ${editsText || "(none)"}; item=${JSON.stringify({
                            label: offered.label,
                            additionalTextEdits: offered.additionalTextEdits,
                        }).slice(0, 2000)}`,
                    );
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// Semantic tokens (the real TextMate engine, same grammar VS Code ships)
// ---------------------------------------------------------------------------

export interface SemanticTokenCase {
    readonly name: string;
    readonly source: string;
    /** Exact token text and the scope fragment it must carry. */
    readonly expectScopes: ReadonlyArray<readonly [text: string, scopeFragment: string]>;
}

type TokenizeFn = (lines: string[]) => Array<{ text: string; scopes: string[] }>;

let grammarReady: Promise<TokenizeFn> | undefined;

function loadGrammarTokenizer(): Promise<TokenizeFn> {
    grammarReady ??= (async () => {
        const root = repoRoot();
        const extensionRoot = path.join(root, "dist", "extensions", "cp3-vscode");
        const grammar = JSON.parse(
            fs.readFileSync(path.join(extensionRoot, "syntaxes", "cp3.tmLanguage.json"), "utf8"),
        ) as { scopeName: string };
        const requireHere = createRequire(path.join(extensionRoot, "scripts", "grammar-smoke.mjs"));
        const oniguruma = requireHere("vscode-oniguruma") as {
            loadWASM(buffer: ArrayBuffer): Promise<void>;
            OnigScanner: new (patterns: string[]) => unknown;
            OnigString: new (s: string) => unknown;
        };
        const textmate = requireHere("vscode-textmate") as {
            Registry: new (options: {
                onigLib: Promise<{
                    createOnigScanner(patterns: string[]): unknown;
                    createOnigString(s: string): unknown;
                }>;
                loadGrammar(scopeName: string): Promise<unknown>;
            }) => {
                loadGrammar(scopeName: string): Promise<{
                    tokenizeLine(
                        line: string,
                        ruleStack: unknown,
                    ): {
                        tokens: Array<{ startIndex: number; endIndex: number; scopes: string[] }>;
                        ruleStack: unknown;
                    };
                } | null>;
            };
            INITIAL: unknown;
        };
        const wasm = fs.readFileSync(path.join(extensionRoot, "node_modules", "vscode-oniguruma", "release", "onig.wasm"));
        const onigLib = oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength)).then(() => ({
            createOnigScanner: (patterns: string[]) => new oniguruma.OnigScanner(patterns),
            createOnigString: (s: string) => new oniguruma.OnigString(s),
        }));
        const registry = new textmate.Registry({
            onigLib,
            loadGrammar: async (scopeName: string) => (scopeName === grammar.scopeName ? grammar : null),
        });
        const loaded = await registry.loadGrammar(grammar.scopeName);
        if (!loaded) {
            throw new Error("the TextMate grammar did not load");
        }
        return (lines: string[]) => {
            const tokens: Array<{ text: string; scopes: string[] }> = [];
            let stack: unknown = textmate.INITIAL;
            for (const line of lines) {
                const result = loaded.tokenizeLine(line, stack);
                stack = result.ruleStack;
                for (const token of result.tokens) {
                    const text = line.slice(token.startIndex, token.endIndex);
                    if (text.trim()) {
                        tokens.push({ text, scopes: token.scopes.slice() });
                    }
                }
            }
            return tokens;
        };
    })();
    return grammarReady;
}

/** Tokenizes `source` with the extension's bundled TextMate grammar. */
export async function semanticTokenize(source: string): Promise<Array<{ text: string; scopes: string[] }>> {
    const tokenize = await loadGrammarTokenizer();
    return tokenize(source.split("\n"));
}

export function semanticTokenCases(suiteName: string, cases: readonly SemanticTokenCase[]): void {
    suite(suiteName, () => {
        for (const spec of cases) {
            test(spec.name, async () => {
                const tokens = await semanticTokenize(spec.source);
                for (const [text, scopeFragment] of spec.expectScopes) {
                    const found = tokens.some((token) => token.text === text && token.scopes.some((scope) => scope.includes(scopeFragment)));
                    assert.ok(
                        found,
                        `expected ${JSON.stringify(text)} to carry ${JSON.stringify(scopeFragment)}; got: ${tokens
                            .map((token) => `${JSON.stringify(token.text)} [${token.scopes.join(" > ")}]`)
                            .join(" | ")}`,
                    );
                }
            });
        }
    });
}
