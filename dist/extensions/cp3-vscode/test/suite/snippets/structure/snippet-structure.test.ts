// Snippets / the contributed cp3 snippet set, checked structurally
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";

interface Snippet {
    prefix?: unknown;
    body?: unknown;
    description?: unknown;
}

const here = __dirname;
// out/test/suite/snippets/<file>.js -> repo root is six levels up from suite/
function repoRoot(): string {
    let dir = here;
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

const root = repoRoot();
const snippetsFile = path.join(root, "dist", "extensions", "cp3-vscode", "snippets", "cp3.json");
const raw = fs.readFileSync(snippetsFile, "utf8");
const parsed = JSON.parse(raw) as Record<string, Snippet>;
const entries = Object.entries(parsed);

suite("Snippets: the contributed cp3 set", () => {
    test("the snippets file parses as JSON", () => {
        assert.ok(entries.length > 0, "expected at least one snippet");
    });

    test("the snippets file contributes at least a hundred entries", () => {
        assert.ok(entries.length >= 100, `expected >= 100 snippets, found ${entries.length}`);
    });

    test("all prefixes are unique across the set", () => {
        const seen = new Map<string, string>();
        for (const [label, snippet] of entries) {
            const prefix = snippet.prefix;
            assert.ok(typeof prefix === "string" || Array.isArray(prefix), `${label}: prefix must be a string or string[]`);
            const prefixes = Array.isArray(prefix) ? prefix : [prefix];
            for (const p of prefixes as string[]) {
                const clash = seen.get(p);
                assert.ok(!clash, `prefix "${p}" used by both ${clash} and ${label}`);
                seen.set(p, label);
            }
        }
    });

    for (const [label, snippet] of entries) {
        test(`snippet "${label}" is well-formed`, () => {
            const prefix = snippet.prefix;
            assert.ok(typeof prefix === "string" || Array.isArray(prefix), "prefix is a string or string[]");
            const prefixes = Array.isArray(prefix) ? (prefix as string[]) : [prefix as string];
            for (const p of prefixes) {
                assert.ok(typeof p === "string" && p.length > 0, "prefix is a non-empty string");
                assert.ok(p.length <= 48, `prefix "${p}" is not absurdly long`);
                assert.ok(!/\s/.test(p), `prefix "${p}" contains no whitespace`);
            }
            assert.ok(typeof snippet.description === "string" && snippet.description.trim().length > 0, "description is present");
            const body = snippet.body;
            const lines = Array.isArray(body) ? (body as unknown[]) : typeof body === "string" ? [body] : undefined;
            assert.ok(lines, "body is a string or string[]");
            for (const line of lines as unknown[]) {
                assert.ok(typeof line === "string", "every body line is a string");
            }
            const text = (lines as string[]).join("\n");
            assert.ok(text.length > 0, "body is not empty");
            assert.ok(!text.includes("\r"), "body has no CR bytes");
            const opens = (text.match(/\$\{/g) ?? []).length;
            const closes = (text.match(/\}/g) ?? []).length;
            assert.ok(closes >= opens, `tabstop placeholders are balanced in "${text.split("\n")[0]}"`);
        });
    }
});
