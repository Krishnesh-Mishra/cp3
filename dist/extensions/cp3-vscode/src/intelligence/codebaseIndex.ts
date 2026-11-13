// dist/extensions/cp3-vscode/src/intelligence/codebaseIndex.ts
//
// A background index of the workspace's .cp3 files that powers the
// high-accuracy suggestion engine. It records three things:
//
//   - unigram frequency: how often each identifier appears anywhere, so a
//     completion can rank `combine` above an equally-good-matching rarer one;
//   - bigram frequency: adjacent token pairs, so after `const` the engine
//     knows the corpus prefers `value` over `variable`, without any hardcoded
//     grammar rules - it learns from this workspace's own code;
//   - member-use frequency: occurrences of `something.member`, so `Math.`
//     can rank `sin` above `acos` in a math-heavy project;
//   - declaration sites: where each identifier is declared and what kind,
//     for workspace symbol suggestions.
//
// Design rules: bounded work (cap on files, batched reads, debounced rebuilds),
// never on the activation event path, open documents always win over the
// on-disk snapshot. The index is a ranking aid; it never emits diagnostics
// and never mutates a document.

import * as vscode from "vscode";

/** One indexed declaration of an identifier. */
export interface IndexedSymbol {
    readonly name: string;
    readonly kind: "function" | "class" | "interface" | "enum" | "type" | "variable";
    readonly uri: vscode.Uri;
    readonly exported: boolean;
}

interface FileRecord {
    /** Text the counts were computed from. */
    readonly text: string;
    /** Identifier occurrence counts in this file. */
    readonly unigram: Map<string, number>;
    /** Adjacent-pair counts in this file (keys joined with a space). */
    readonly pairs: Map<string, number>;
    /** `x.member` occurrence counts. */
    readonly memberUse: Map<string, number>;
    /** Declarations found in this file. */
    readonly symbols: readonly IndexedSymbol[];
}

const TOKEN = /[A-Za-z_$][A-Za-z0-9_$]*/g;
const DECLARATION = /^\s*(?:export\s+)?(?:(?:declare|default|async|abstract)\s+)*(function|class|interface|enum|type|const|let|var)\s+([A-Za-z_$][\w$]*)/gm;
const EXPORT_LIST = /^\s*export\s*\{([^}]+)\}/gm;
const MAX_FILES = 2500;
const BATCH = 32;

function emptyRecord(uri: vscode.Uri, text: string): FileRecord {
    const unigram = new Map<string, number>();
    const pairs = new Map<string, number>();
    const memberUse = new Map<string, number>();

    TOKEN.lastIndex = 0;
    let previous: string | undefined;
    let lastEnd = 0;
    for (let match = TOKEN.exec(text); match; match = TOKEN.exec(text)) {
        const token = match[0];
        const between = text.slice(lastEnd, match.index);
        // `x.member` pairs are not adjacent-word bigrams; count them separately.
        const isMemberUse = /^\s*\.\s*$/.test(between);
        if (isMemberUse) {
            memberUse.set(token, (memberUse.get(token) ?? 0) + 1);
        }
        unigram.set(token, (unigram.get(token) ?? 0) + 1);
        if (previous !== undefined && !isMemberUse) {
            const key = `${previous} ${token}`;
            pairs.set(key, (pairs.get(key) ?? 0) + 1);
        }
        previous = token;
        lastEnd = match.index + token.length;
    }

    const symbols: IndexedSymbol[] = [];
    DECLARATION.lastIndex = 0;
    for (let match = DECLARATION.exec(text); match; match = DECLARATION.exec(text)) {
        const rawKind = match[1];
        const name = match[2];
        if (name === "default") continue;
        const exported = /^\s*export\s/.test(match[0]) || match[0].includes(" export ");
        symbols.push({ name, kind: kindFor(rawKind), uri, exported });
    }
    EXPORT_LIST.lastIndex = 0;
    for (let match = EXPORT_LIST.exec(text); match; match = EXPORT_LIST.exec(text)) {
        // Every name in `export { a, b as c }` is exported from this file.
        for (const part of match[1].split(",")) {
            const cleaned = part.trim();
            const alias = cleaned.split(/\s+as\s+/).pop()?.trim();
            if (!alias || !/^[A-Za-z_$][\w$]*$/.test(alias) || alias === "default") continue;
            symbols.push({
                name: alias,
                kind: "variable",
                uri,
                exported: true,
            });
        }
    }
    return { text, unigram, pairs, memberUse, symbols };
}

function kindFor(raw: string): IndexedSymbol["kind"] {
    switch (raw) {
        case "function": return "function";
        case "class": return "class";
        case "interface": return "interface";
        case "enum": return "enum";
        case "type": return "type";
        default: return "variable";
    }
}

function mergeMap(target: Map<string, number>, source: Map<string, number>): void {
    for (const [key, value] of source) {
        target.set(key, (target.get(key) ?? 0) + value);
    }
}

export class CodebaseIndex implements vscode.Disposable {
    private records = new Map<string, FileRecord>();
    private unigram = new Map<string, number>();
    private pairs = new Map<string, number>();
    private memberUse = new Map<string, number>();
    private symbols = new Map<string, IndexedSymbol[]>();
    private rebuilding = false;
    private rebuildQueued = false;
    private rebuildTimer: ReturnType<typeof setTimeout> | undefined;
    private watcher: vscode.FileSystemWatcher | undefined;
    private readonly changeEmitter = new vscode.EventEmitter<void>();
    /** Fires after a full or incremental rebuild lands. */
    readonly onDidUpdate = this.changeEmitter.event;

    constructor() {
        // Watch for structural changes; content changes are folded in on the
        // debounce as well (a full re-scan is simpler and fast enough at
        // workspace scale, and keeps even large edits correct).
        this.watcher = vscode.workspace.createFileSystemWatcher("**/*.cp3");
        this.watcher.onDidCreate(() => this.scheduleRebuild());
        this.watcher.onDidChange(() => this.scheduleRebuild());
        this.watcher.onDidDelete(() => this.scheduleRebuild());
    }

    /** Kicks off a background rebuild if one is not already running. */
    start(): void {
        this.scheduleRebuild();
    }

    /** Rebuild on the next debounce tick; open documents override stale disk text. */
    refreshSoon(): void {
        this.scheduleRebuild();
    }

    dispose(): void {
        if (this.rebuildTimer) clearTimeout(this.rebuildTimer);
        this.watcher?.dispose();
        this.changeEmitter.dispose();
    }

    private scheduleRebuild(): void {
        if (this.rebuildTimer) clearTimeout(this.rebuildTimer);
        // A short debounce coalesces the burst of watcher events from a save.
        this.rebuildTimer = setTimeout(() => void this.rebuild(), 250);
    }

    private async rebuild(): Promise<void> {
        if (this.rebuilding) {
            this.rebuildQueued = true;
            return;
        }
        this.rebuilding = true;
        try {
            const next = new Map<string, FileRecord>();
            const files = await vscode.workspace.findFiles(
                "**/*.cp3",
                "**/{build,dist,node_modules,.git,third_party,generated}/**",
                MAX_FILES,
            );
            for (let start = 0; start < files.length; start += BATCH) {
                const batch = files.slice(start, start + BATCH);
                const results = await Promise.all(batch.map(async (file) => {
                    try {
                        const bytes = await vscode.workspace.fs.readFile(file);
                        return { uri: file, text: Buffer.from(bytes).toString("utf8") };
                    } catch {
                        return undefined;
                    }
                }));
                for (const result of results) {
                    if (!result) continue;
                    next.set(normalizedPath(result.uri), emptyRecord(result.uri, result.text));
                }
            }
            // Open documents override any stale disk state with their live text.
            for (const document of vscode.workspace.textDocuments) {
                if (document.languageId !== "cp3" || document.uri.scheme !== "file") continue;
                next.set(normalizedPath(document.uri), emptyRecord(document.uri, document.getText()));
            }
            this.records = next;
            this.recomputeAggregates();
            this.changeEmitter.fire();
        } finally {
            this.rebuilding = false;
            if (this.rebuildQueued) {
                this.rebuildQueued = false;
                this.scheduleRebuild();
            }
        }
    }

    private recomputeAggregates(): void {
        const unigram = new Map<string, number>();
        const pairs = new Map<string, number>();
        const memberUse = new Map<string, number>();
        const symbols = new Map<string, IndexedSymbol[]>();
        for (const record of this.records.values()) {
            mergeMap(unigram, record.unigram);
            mergeMap(pairs, record.pairs);
            mergeMap(memberUse, record.memberUse);
            for (const symbol of record.symbols) {
                const list = symbols.get(symbol.name);
                if (list) list.push(symbol);
                else symbols.set(symbol.name, [symbol]);
            }
        }
        this.unigram = unigram;
        this.pairs = pairs;
        this.memberUse = memberUse;
        this.symbols = symbols;
    }

    /** Total occurrences of `name` anywhere in the indexed corpus. */
    usage(name: string): number {
        return this.unigram.get(name) ?? 0;
    }

    /**
     * Corpus frequency of `previous -> next` as an adjacent token pair.
     * Used to rank what tends to follow the token before the cursor.
     */
    pairUsage(previous: string | undefined, next: string): number {
        if (previous === undefined) return 0;
        return this.pairs.get(`${previous} ${next}`) ?? 0;
    }

    /** Corpus frequency of `x.member` occurrences. */
    memberUsage(member: string): number {
        return this.memberUse.get(member) ?? 0;
    }

    /**
     * All exported symbols in the workspace whose name starts with `prefix`.
     * Unexported declarations are only visible inside their own module, and
     * the current file is filtered by the caller.
     */
    searchSymbols(prefix: string, limit: number): IndexedSymbol[] {
        const wanted = prefix.toLowerCase();
        const hits: IndexedSymbol[] = [];
        for (const [name, entries] of this.symbols) {
            if (!name.toLowerCase().startsWith(wanted)) continue;
            for (const entry of entries) {
                if (entry.exported) hits.push(entry);
            }
            if (hits.length >= limit * 4) break;
        }
        hits.sort((a, b) => {
            const exported = Number(b.exported) - Number(a.exported);
            if (exported !== 0) return exported;
            return this.usage(b.name) - this.usage(a.name);
        });
        return hits.slice(0, limit);
    }

    /** Number of indexed files (for diagnostics and tests). */
    get indexedFiles(): number {
        return this.records.size;
    }
}

export function normalizedPath(uri: vscode.Uri): string {
    return uri.fsPath.replace(/\\/g, "/").toLowerCase();
}

/** Memoizes per-document tokenization between completion requests. */
const liveTokenCache = new WeakMap<object, { version: number; tokens: string[] }>();

/**
 * Tokens of the given text, cached per (document version) by the caller —
 * implementations should pass `document.version` as `key` and `document` as `doc`.
 */
export function documentTokens(document: vscode.TextDocument): string[] {
    const cached = liveTokenCache.get(document);
    if (cached && cached.version === document.version) {
        return cached.tokens;
    }
    const text = document.getText();
    const tokens: string[] = [];
    TOKEN.lastIndex = 0;
    for (let match = TOKEN.exec(text); match; match = TOKEN.exec(text)) {
        tokens.push(match[0]);
    }
    liveTokenCache.set(document, { version: document.version, tokens });
    return tokens;
}

/** The token right before `offset` in `text`, or undefined at the start of input. */
export function tokenBeforeOffset(text: string, offset: number): string | undefined {
    let i = Math.min(offset, text.length) - 1;
    while (i >= 0 && !isWordChar(text[i])) i--;
    if (i < 0) return undefined;
    let end = i + 1;
    while (i >= 0 && isWordChar(text[i])) i--;
    return text.slice(i + 1, end);
}

function isWordChar(ch: string): boolean {
    return /[A-Za-z0-9_$]/.test(ch);
}
