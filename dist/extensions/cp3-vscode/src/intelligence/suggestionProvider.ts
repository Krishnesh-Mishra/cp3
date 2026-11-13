// dist/extensions/cp3-vscode/src/intelligence/suggestionProvider.ts
//
// The smart suggestion engine for cp3. It runs alongside the language
// server (never instead of it) and offers the candidates the server cannot
// rank well on its own:
//
//   1. Member completion after `x.`: the documented closed stdlib surface,
//      resolved through the document's own import bindings (any alias), with
//      a sortText bucket fed by this workspace's member-use frequency and a
//      documented static prior for hot members (sin/cos/log/...). The user
//      with no import at all still gets the member list; accepting one adds
//      the import for them.
//   2. Identifier completion: named stdlib exports (Buffer, Map, URL, ...)
//      offered with an auto-import edit, "module member" call snippets
//      (`console.log(...)` for someone who typed `cons`) built from the
//      catalog and ranked by corpus usage, and the workspace's own declared
//      symbols ranked by frequency.
//
// Ranking is corpus-driven: the engine tokenizes the workspace (bounded,
// background, debounced) and learns "after `const` comes `value` here" from
// this codebase's own bigrams. Nothing about the language itself is
// hardcoded beyond the closed stdlib tables.

import * as vscode from "vscode";

import { importInsertion, isCommentOrStringAt, localBindings } from "../editorFeatures";
import {
    CodebaseIndex,
    normalizedPath,
    tokenBeforeOffset,
} from "./codebaseIndex";
import {
    DEFAULT_MODULES,
    NAMED_EXPORTS,
    STDLIB_MODULES,
    findDefaultModule,
    type StdlibMember,
    type StdlibModule,
} from "./stdlibCatalog";
import { compositeScore, matchRank, sortTextFor } from "./ranking";

const MAX_MEMBER_ITEMS = 100;
const MAX_NAMED_ITEMS = 8;
const MAX_MEMBER_CALL_ITEMS = 8;
const MAX_SYMBOL_ITEMS = 24;
const MAX_TOTAL_ITEMS = 48;

/** Static priors for the members people actually call, used when the corpus is silent. */
const MEMBER_PRIOR: Readonly<Record<string, Readonly<Record<string, number>>>> = {
    "std/console": { log: 30, error: 24, warn: 20, info: 16, input: 12, clear: 8, assert: 10, time: 8 },
    "std/math": { sin: 26, cos: 26, tan: 20, sqrt: 24, abs: 22, floor: 20, ceil: 16, round: 18, trunc: 12, cbrt: 8, random: 20, min: 18, max: 18, pow: 14, hypot: 10, clamp: 10, lerp: 8, exp: 6, log: 12, log2: 6, log10: 6, PI: 16 },
    "std/json": { parse: 28, stringify: 28, parseFileSync: 8, stringifyToFile: 4 },
    "std/process": { exit: 22, env: 22, argv: 18, cwd: 10, on: 14, spawn: 10, exec: 10, nextTick: 8 },
    "std/fs": { readFileSync: 16, writeFileSync: 16, existsSync: 14, readdirSync: 10, mkdirSync: 8, readFile: 14, writeFile: 14, statSync: 8 },
    "std/path": { join: 24, resolve: 14, basename: 10, dirname: 10, extname: 12, normalize: 8 },
    "std/crypto": { randomUUID: 16, sha256: 14, hmacSha256: 8, randomBytes: 10 },
    "std/date": { now: 22, fromISOString: 8, parse: 8 },
    "std/os": { cpus: 10, totalMemory: 8, hostname: 8, platform: 10 },
    "std/time": { stopwatch: 14 },
    "std/readline": { readLine: 16, readLines: 10, readAll: 10, createReader: 6 },
    "std/hash": { crc32: 12, xxhash64: 10, fnv1a64: 6 },
    "std/io": { waitReadable: 12, waitWritable: 12, pollUntilDone: 8 },
    "std/url": { encodeURIComponent: 16, URL: 14, URLSearchParams: 8 },
    "std/text": { TextEncoder: 14, TextDecoder: 10 },
    "std/buffer": { Buffer: 20 },
    "std/collections": { Map: 22, Set: 18, LRUCache: 8, Queue: 8, Stack: 8 },
    "std/env": { get: 20, has: 12, load: 8 },
};

function memberPrior(module: StdlibModule, member: string): number {
    return MEMBER_PRIOR[module.specifier]?.[member] ?? 0;
}

/** Parsed view of the current document's imports. */
interface ImportInfo {
    /** local binding name -> module specifier (default and namespace imports). */
    readonly defaults: ReadonlyMap<string, string>;
    /** Names bound through named imports (e.g. Map from std/collections). */
    readonly named: ReadonlySet<string>;
    /** Existing named-import lines per specifier: `import { a, b } from "spec"`. */
    readonly namedLines: ReadonlyMap<string, vscode.Range>;
}

const IMPORT_LINE = /^(\s*)import\s+([^;]+?)\s+from\s+["']([^"']+)["']\s*;?\s*$/gm;

function scanImports(document: vscode.TextDocument): ImportInfo {
    const text = document.getText();
    const defaults = new Map<string, string>();
    const named = new Set<string>();
    const namedLines = new Map<string, vscode.Range>();
    IMPORT_LINE.lastIndex = 0;
    for (let match = IMPORT_LINE.exec(text); match; match = IMPORT_LINE.exec(text)) {
        const clause = match[2].trim();
        const specifier = match[3];
        if (clause.startsWith("{")) {
            // import { a, b as c } from "spec";
            for (const part of clause.replace(/^\{|\}$/g, "").split(",")) {
                const alias = part.trim().split(/\s+as\s+/).pop()?.trim();
                if (alias && /^[A-Za-z_$][\w$]*$/.test(alias)) {
                    named.add(alias);
                }
            }
            const line = document.positionAt(match.index).line;
            namedLines.set(specifier, new vscode.Range(line, 0, line, document.lineAt(line).text.length));
            continue;
        }
        const star = clause.match(/^\*\s+as\s+([A-Za-z_$][\w$]*)/);
        if (star) {
            defaults.set(star[1], specifier);
            continue;
        }
        // import Name from "spec";  or  import Name, { a, b } from "spec";
        const defaultPart = clause.split(",", 1)[0].trim();
        if (/^[A-Za-z_$][\w$]*$/.test(defaultPart)) {
            defaults.set(defaultPart, specifier);
        }
        const namedMatch = clause.match(/\{([^}]*)\}/);
        if (namedMatch) {
            for (const part of namedMatch[1].split(",")) {
                const alias = part.trim().split(/\s+as\s+/).pop()?.trim();
                if (alias && /^[A-Za-z_$][\w$]*$/.test(alias)) {
                    named.add(alias);
                }
            }
            const line = document.positionAt(match.index).line;
            namedLines.set(specifier, new vscode.Range(line, 0, line, document.lineAt(line).text.length));
        }
    }
    return { defaults, named, namedLines };
}

function kindForMember(kind: "function" | "constant" | "property"): vscode.CompletionItemKind {
    switch (kind) {
        case "function": return vscode.CompletionItemKind.Function;
        case "constant": return vscode.CompletionItemKind.Constant;
        default: return vscode.CompletionItemKind.Property;
    }
}

export class Cp3SuggestionProvider implements vscode.CompletionItemProvider {
    constructor(private readonly index: CodebaseIndex) {}

    provideCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position,
        token: vscode.CancellationToken,
    ): vscode.CompletionItem[] {
        if (token.isCancellationRequested) return [];
        if (document.languageId !== "cp3") return [];
        if (!vscode.workspace.getConfiguration("cp3").get<boolean>("suggestCodebase", true)) return [];
        const text = document.getText();
        const offset = document.offsetAt(position);
        if (isCommentOrStringAt(text, offset)) return [];
        const linePrefix = document.lineAt(position.line).text.slice(0, position.character);
        if (/^\s*import\b/.test(linePrefix)) return [];

        const imports = scanImports(document);

        const memberMatch = linePrefix.match(/([A-Za-z_$][\w$]*)\s*\.\s*([A-Za-z_$][\w$]*)?$/);
        if (memberMatch) {
            return this.memberSuggestions(document, position, imports, memberMatch[1], memberMatch[2] ?? "");
        }

        const wordMatch = linePrefix.match(/[A-Za-z_$][\w$]*$/);
        if (!wordMatch) return [];
        const query = wordMatch[0];
        return this.identifierSuggestions(document, position, imports, query, wordMatch.index ?? 0);
    }

    /** Member completion for `receiver.<query>` against the stdlib catalog. */
    private memberSuggestions(
        document: vscode.TextDocument,
        position: vscode.Position,
        imports: ImportInfo,
        receiver: string,
        query: string,
    ): vscode.CompletionItem[] {
        // Resolve the receiver to a catalog module: first via the document's
        // own imports (any alias), then via the documented conventional name.
        let module: StdlibModule | undefined;
        let importedBinding: string | undefined;
        const specifier = imports.defaults.get(receiver);
        if (specifier !== undefined) {
            module = STDLIB_MODULES.find((candidate) => candidate.specifier === specifier);
            importedBinding = receiver;
        } else {
            module = findDefaultModule(receiver);
            // A bare `Math.`/`console.` without an import is only the module if
            // the file doesn't bind the name to something else. If it does
            // (e.g. `const Math = 3`), offering stdlib members would be wrong;
            // that receiver is the language server's domain.
            if (module !== undefined && localBindings(document.getText()).has(receiver)) {
                return [];
            }
        }
        if (module === undefined) {
            return []; // an unknown receiver: the language server's domain
        }
        const needsImport = importedBinding === undefined;
        const items: vscode.CompletionItem[] = [];
        for (const member of module.members) {
            const matched = matchRank(query, member.name);
            if (!matched.ok) continue;
            const usage = this.index.memberUsage(member.name);
            const prior = memberPrior(module, member.name);
            const score = matched.rank + 200 - Math.min(80, Math.floor(Math.log10(1 + usage) * 26)) - Math.min(40, prior);
            const item = new vscode.CompletionItem(member.name, kindForMember(member.kind));
            item.detail = `${member.signature}  ·  ${module.specifier}`;
            const documentation = new vscode.MarkdownString();
            documentation.appendCodeblock(member.signature, "cp3");
            documentation.appendMarkdown(member.doc);
            if (needsImport) {
                const binding = module.defaultName;
                documentation.appendMarkdown(
                    `\n\nInserts \`import ${binding} from "${module.specifier}";\` if it is missing.`,
                );
            }
            item.documentation = documentation;
            item.sortText = sortTextFor(score);
            item.filterText = member.name;
            if (query.length > 0) {
                item.range = new vscode.Range(
                    position.translate(0, -query.length),
                    position,
                );
            }
            if (needsImport) {
                const binding = module.defaultName;
                const insertion = importInsertion(document);
                item.additionalTextEdits = [new vscode.TextEdit(
                    new vscode.Range(insertion, insertion),
                    `import ${binding} from "${module.specifier}";\n`,
                )];
            }
            items.push(item);
            if (items.length >= MAX_MEMBER_ITEMS) break;
        }
        // Highest-ranked item first; stable for equal scores by member order.
        items.sort((a, b) => (a.sortText ?? "").localeCompare(b.sortText ?? ""));
        return items;
    }

    /** Identifier-position completion: stdlib named exports, module-member
     *  call snippets and workspace symbols. */
    private identifierSuggestions(
        document: vscode.TextDocument,
        position: vscode.Position,
        imports: ImportInfo,
        query: string,
        wordStart: number,
    ): vscode.CompletionItem[] {
        const text = document.getText();
        const queryLower = query.toLowerCase();
        const wordStartOffset = document.offsetAt(new vscode.Position(position.line, wordStart));
        const prevToken = tokenBeforeOffset(text, wordStartOffset);
        const items: vscode.CompletionItem[] = [];

        const bound = localBindings(text);

        // (a) Named stdlib exports (Map, URL, TextEncoder, ...) with an
        // auto-import edit. Cap the bucket; prefix matches only.
        if (query.length >= 2) {
            for (const module of NAMED_EXPORTS) {
                if (items.length >= MAX_NAMED_ITEMS) break;
                for (const member of module.members) {
                    if (items.length >= MAX_NAMED_ITEMS) break;
                    const name = member.name;
                    if (!name.toLowerCase().startsWith(queryLower)) continue;
                    if (imports.named.has(name) || bound.has(name)) continue;
                    const usage = this.index.usage(name);
                    const pair = this.index.pairUsage(prevToken, name);
                    const score = compositeScore({
                        query,
                        candidate: name,
                        corpusUsage: usage + 4, // a small prior: these exist to be imported
                        pairUsage: pair,
                        staticPrior: 8,
                    });
                    if (score === undefined) continue;
                    const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
                    item.detail = `${member.signature}  ·  ${module.specifier} (auto-import)`;
                    item.documentation = new vscode.MarkdownString(
                        `${member.doc}\n\nImports \`${name}\` from \`${module.specifier}\`.`,
                    );
                    item.sortText = sortTextFor(score);
                    item.additionalTextEdits = [this.importNamedEdit(document, imports, module, name)];
                    items.push(item);
                }
            }
        }

        // (b) "module member" snippets: for someone who typed `cons`, offer
        // `console.log(...)` (and friends) ranked by how this workspace uses
        // them; the accept inserts the call and the import together.
        if (query.length >= 2) {
            const memberCallItems = this.memberCallSuggestions(document, position, imports, query, prevToken, bound);
            items.push(...memberCallItems);
        }

        // (c) Workspace symbols from the background index, excluding names
        // this file binds itself (the server offers those).
        const thisFile = normalizedPath(document.uri);
        const seen = new Set<string>();
        let symbolItems = 0;
        for (const symbol of this.index.searchSymbols(query, MAX_SYMBOL_ITEMS * 2)) {
            if (symbolItems >= MAX_SYMBOL_ITEMS) break;
            if (items.length >= MAX_TOTAL_ITEMS) break;
            if (seen.has(symbol.name)) continue;
            seen.add(symbol.name);
            if (normalizedPath(symbol.uri) === thisFile) continue;
            if (bound.has(symbol.name)) continue;
            const usage = this.index.usage(symbol.name);
            const pair = this.index.pairUsage(prevToken, symbol.name);
            const score = compositeScore({ query, candidate: symbol.name, corpusUsage: usage, pairUsage: pair });
            if (score === undefined) continue;
            const workspacePath = vscode.workspace.asRelativePath(symbol.uri, false);
            const item = new vscode.CompletionItem(symbol.name, this.kindForSymbol(symbol.kind));
            item.detail = `${symbol.exported ? "exported " : ""}${symbol.kind} · ${workspacePath}`;
            item.documentation = new vscode.MarkdownString(
                `Declared in \`${workspacePath}\`. ${usage > 1 ? `Used ${usage} times in this workspace.` : ""}`,
            );
            item.sortText = sortTextFor(score + 40); // symbols rank below stdlib hints
            items.push(item);
            symbolItems += 1;
        }

        items.sort((a, b) => (a.sortText ?? "").localeCompare(b.sortText ?? ""));
        return items.slice(0, MAX_TOTAL_ITEMS);
    }

    private memberCallSuggestions(
        document: vscode.TextDocument,
        position: vscode.Position,
        imports: ImportInfo,
        query: string,
        prevToken: string | undefined,
        bound: Set<string>,
    ): vscode.CompletionItem[] {
        const queryLower = query.toLowerCase();
        const wordRange = new vscode.Range(
            position.translate(0, -query.length),
            position,
        );
        const candidates: Array<{ module: StdlibModule; member: StdlibMember; matchVia: "module" | "member"; localBinding: string | undefined }> = [];
        for (const module of DEFAULT_MODULES) {
            const importedBinding = this.localBindingFor(module, imports);
            const localName = importedBinding ?? module.defaultName;
            if (importedBinding === undefined && bound.has(localName)) {
                // The local name is taken by something that is not the module.
                // Don't offer inserts against it; skip this module entirely.
                continue;
            }
            const moduleMatch = module.defaultName.toLowerCase().startsWith(queryLower) ||
                (module.aliases ?? []).some((alias) => alias.toLowerCase().startsWith(queryLower));
            for (const member of module.members) {
                const memberMatch = member.name.toLowerCase().startsWith(queryLower);
                if (!moduleMatch && !memberMatch) continue;
                // Member-prefix matches need at least 3 characters to avoid noise.
                if (!moduleMatch && query.length < 3) continue;
                candidates.push({ module, member, matchVia: moduleMatch ? "module" : "member", localBinding: importedBinding });
            }
        }
        // Score and cap.
        const scored = candidates
            .map((candidate) => {
                const { module, member } = candidate;
                const usage = this.index.memberUsage(member.name);
                const prior = memberPrior(module, member.name);
                const pair = this.index.pairUsage(prevToken, member.name);
                // A member matched via the module name ("cons" -> console.log)
                // gets the module prior; a member matched by name uses the
                // member prior only.
                const base = candidate.matchVia === "module" ? prior + 10 : prior;
                const score = 210 - Math.min(80, Math.floor(Math.log10(1 + usage) * 26)) - Math.min(60, Math.floor(Math.log10(1 + pair) * 30)) - Math.min(40, base);
                return { candidate, score };
            })
            .sort((a, b) => a.score - b.score)
            .slice(0, MAX_MEMBER_CALL_ITEMS);
        return scored.map(({ candidate, score }) => {
            const { module, member, localBinding, matchVia } = candidate;
            const localName = localBinding ?? module.defaultName;
            const needsImport = localBinding === undefined;
            // A member matched by its own name ("sin" -> Math.sin) is labelled
            // by the member so it surfaces for member-prefix queries; a member
            // matched via the module name ("cons" -> console.log) stays labelled
            // module.member so it surfaces for module-prefix queries.
            const label = matchVia === "member"
                ? (member.kind === "function" ? `${member.name}()` : member.name)
                : (member.kind === "function" ? `${localName}.${member.name}()` : `${localName}.${member.name}`);
            const item = new vscode.CompletionItem(
                label,
                member.kind === "function" ? vscode.CompletionItemKind.Snippet : kindForMember(member.kind),
            );
            item.insertText = member.kind === "function"
                ? new vscode.SnippetString(`${localName}.${member.name}($0)`)
                : `${localName}.${member.name}`;
            item.detail = `${member.signature}  ·  ${module.specifier}${needsImport ? " (auto-import)" : ""}`;
            const documentation = new vscode.MarkdownString();
            documentation.appendCodeblock(member.signature, "cp3");
            documentation.appendMarkdown(`${member.doc}\n\n${module.summary}`);
            if (needsImport) {
                documentation.appendMarkdown(`\n\nInserts \`import ${localName} from "${module.specifier}";\`.`);
            }
            item.documentation = documentation;
            item.range = wordRange;
            item.sortText = sortTextFor(score);
            if (needsImport) {
                const insertion = importInsertion(document);
                item.additionalTextEdits = [new vscode.TextEdit(
                    new vscode.Range(insertion, insertion),
                    `import ${localName} from "${module.specifier}";\n`,
                )];
            }
            return item;
        });
    }

    private localBindingFor(module: StdlibModule, imports: ImportInfo): string | undefined {
        for (const [binding, specifier] of imports.defaults) {
            if (specifier === module.specifier) return binding;
        }
        return undefined;
    }

    private importNamedEdit(
        document: vscode.TextDocument,
        imports: ImportInfo,
        module: StdlibModule,
        name: string,
    ): vscode.TextEdit {
        const existing = imports.namedLines.get(module.specifier);
        if (existing) {
            const line = document.lineAt(existing.start.line).text;
            const brace = line.match(/\{([^}]*)\}/);
            const inner = brace ? brace[1].trim() : "";
            const merged = inner.length > 0 ? `${inner}, ${name}` : name;
            const replacement = line.replace(/\{[^}]*\}/, `{ ${merged} }`);
            return new vscode.TextEdit(existing, replacement);
        }
        const insertion = importInsertion(document);
        return new vscode.TextEdit(
            new vscode.Range(insertion, insertion),
            `import { ${name} } from "${module.specifier}";\n`,
        );
    }

    private kindForSymbol(kind: "function" | "class" | "interface" | "enum" | "type" | "variable"): vscode.CompletionItemKind {
        switch (kind) {
            case "function": return vscode.CompletionItemKind.Function;
            case "class": return vscode.CompletionItemKind.Class;
            case "interface": return vscode.CompletionItemKind.Interface;
            case "enum": return vscode.CompletionItemKind.Enum;
            case "type": return vscode.CompletionItemKind.Struct;
            default: return vscode.CompletionItemKind.Variable;
        }
    }
}

/** Registers the provider; returns the disposable for the caller's subscriptions. */
export function registerSuggestionEngine(context: vscode.ExtensionContext): void {
    const index = new CodebaseIndex();
    context.subscriptions.push(index);
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((document) => {
        if (document.languageId === "cp3") index.refreshSoon();
    }));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument((event) => {
        // The rebuild is debounced and prefers open documents' live text, so
        // bursty typing coalesces into one re-scan once the user pauses.
        if (event.document.languageId === "cp3" && event.document.uri.scheme === "file") {
            index.refreshSoon();
        }
    }));
    index.start();

    const triggers = Array.from(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$.",
    );
    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(
        { language: "cp3" },
        new Cp3SuggestionProvider(index),
        ...triggers,
    ));
}
