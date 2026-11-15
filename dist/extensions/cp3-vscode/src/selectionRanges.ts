// dist/extensions/cp3-vscode/src/selectionRanges.ts
//
// A selection-range provider for cp3 documents. Selecting a token expands
// one level at a time: the identifier (or literal) under the cursor, then
// the expression that contains it. The extension does not reimplement a
// parser here; it walks text boundaries (word/literal tokens, expression
// operators, brackets) which is enough to give the nesting the editor
// shows on "expand selection".
//
// The ranges returned for a position must form a chain of STRICTLY
// INCLUSIVE ranges, innermost first, each linked to its parent through the
// SelectionRange.parent field (the extension host rejects the chain
// otherwise). The parent links do not survive the command's flattening,
// which is why the nesting the editor shows is a chain of single ranges.

import * as vscode from "vscode";

const EXPRESSION_HEAD = /[A-Za-z0-9_$]/;
const EXPRESSION_CHARS = /[A-Za-z0-9_$+\-*/%&|^!~<>=?:.{}()\[\]'"`]/;
const OPENERS: ReadonlyMap<string, string> = new Map([["(", ")"], ["[", "]"], ["{", "}"]]);
const CLOSERS: ReadonlyMap<string, string> = new Map([[")", "("], ["]", "["], ["}", "{"]]);

function isExpressionChar(ch: string): boolean {
    return EXPRESSION_CHARS.test(ch);
}

function expand(text: string, start: number, end: number): [number, number] {
    // Left edge: consume the run of expression characters and spaces that
    // continue the same expression (so `const sum = n * 2;` expands from
    // `n` through the whole initializer, not just the identifier).
    for (;;) {
        let j = start - 1;
        if (j < 0) break;
        const ch = text[j];
        if (isExpressionChar(ch)) {
            start = j;
            continue;
        }
        if (ch === " " || ch === "\t") {
            let k = j;
            while (k >= 0 && (text[k] === " " || text[k] === "\t")) k -= 1;
            if (k >= 0 && isExpressionChar(text[k])) {
                start = k + 1;
                continue;
            }
            break;
        }
        break;
    }
    // Right edge, symmetric.
    for (;;) {
        let j = end;
        if (j >= text.length) break;
        const ch = text[j];
        if (isExpressionChar(ch)) {
            end = j + 1;
            continue;
        }
        if (ch === " " || ch === "\t") {
            let k = j;
            while (k < text.length && (text[k] === " " || text[k] === "\t")) k += 1;
            if (k < text.length && isExpressionChar(text[k])) {
                end = k;
                continue;
            }
            break;
        }
        break;
    }
    return [start, end];
}

/**
 * The innermost bracket pair (`[...]`, `(...)`, `{...}`) that strictly
 * contains [start, end), or undefined. Scans the text once, skipping
 * string and template contents so brackets inside them do not pair up.
 */
function enclosingBracket(text: string, start: number, end: number): [number, number] | undefined {
    const stack: Array<{ open: string; at: number }> = [];
    const pairs: Array<[number, number]> = [];
    let quote: string | undefined;
    for (let i = 0; i < text.length; i++) {
        const ch = text[i];
        if (quote) {
            if (ch === "\\") {
                i += 1; // skip the escape
                continue;
            }
            if (ch === quote) quote = undefined;
            continue;
        }
        if (ch === "/" && text[i + 1] === "/") {
            while (i < text.length && text[i] !== "\n") i += 1;
            continue;
        }
        if (ch === '"' || ch === "'" || ch === "`") {
            quote = ch;
            continue;
        }
        if (OPENERS.has(ch)) {
            stack.push({ open: ch, at: i });
            continue;
        }
        if (CLOSERS.has(ch)) {
            const opener = CLOSERS.get(ch);
            for (let k = stack.length - 1; k >= 0; k--) {
                if (stack[k].open === opener) {
                    pairs.push([stack[k].at, i]);
                    stack.splice(k);
                    break;
                }
            }
        }
    }
    let best: [number, number] | undefined;
    for (const pair of pairs) {
        const [open, close] = pair;
        if (open < start && close + 1 >= end) {
            if (!best || open > best[0]) best = pair; // closest pair wins (innermost)
        }
    }
    return best ? [best[0], best[1] + 1] : undefined;
}

function tokenAt(text: string, offset: number): [number, number] | undefined {
    let start = offset;
    while (start > 0 && EXPRESSION_HEAD.test(text[start - 1])) start -= 1;
    let end = offset;
    while (end < text.length && EXPRESSION_HEAD.test(text[end])) end += 1;
    if (start === end) return undefined;
    return [start, end];
}

class Cp3SelectionRangeProvider implements vscode.SelectionRangeProvider {
    provideSelectionRanges(
        document: vscode.TextDocument,
        positions: vscode.Position[],
    ): vscode.SelectionRange[] {
        const text = document.getText();
        const results: vscode.SelectionRange[] = [];
        for (const position of positions) {
            const offset = document.offsetAt(position);
            const token = tokenAt(text, offset);
            if (!token) {
                // Not on a token: a one-column range still needs a non-empty chain.
                const zero = new vscode.Range(position, position);
                results.push(new vscode.SelectionRange(zero));
                continue;
            }
            const [tokenStart, tokenEnd] = token;
            // The parent: the smaller of the bracket literal that contains
            // the token and the expression walk. A walk cannot see past
            // commas inside a literal (`[1, 2, 3]`), so the bracket wins
            // there; inside a class body the brace is wider than the walk,
            // so the walk wins.
            const bracket = enclosingBracket(text, tokenStart, tokenEnd);
            const [exprStart, exprEnd] = expand(text, tokenStart, tokenEnd);
            const walkDiffers = exprStart !== tokenStart || exprEnd !== tokenEnd;
            const bracketWidth = bracket ? bracket[1] - bracket[0] : Number.POSITIVE_INFINITY;
            const walkWidth = exprEnd - exprStart;
            let parent: [number, number];
            if (bracket && (!walkDiffers || bracketWidth < walkWidth)) {
                parent = bracket;
            } else if (walkDiffers) {
                parent = [exprStart, exprEnd];
            } else {
                // Fallback: extend to the end of the line, which always
                // strictly contains the token.
                const line = document.lineAt(position.line);
                parent = [document.offsetAt(line.range.start), document.offsetAt(line.range.end)];
            }
            const parentRange = new vscode.Range(
                document.positionAt(parent[0]),
                document.positionAt(parent[1]),
            );
            const tokenRange = new vscode.SelectionRange(
                new vscode.Range(document.positionAt(tokenStart), document.positionAt(tokenEnd)),
                new vscode.SelectionRange(parentRange),
            );
            results.push(tokenRange);
        }
        return results;
    }
}

export function registerSelectionRanges(context: vscode.ExtensionContext): void {
    context.subscriptions.push(
        vscode.languages.registerSelectionRangeProvider(
            { language: "cp3" },
            new Cp3SelectionRangeProvider(),
        ),
    );
}
