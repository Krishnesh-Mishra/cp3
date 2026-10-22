// extensions/cp3-vscode/scripts/grammar-smoke.mjs
//
// Loads syntaxes/cp3.tmLanguage.json through the real TextMate engine
// (vscode-textmate + vscode-oniguruma, the same pair VS Code uses) and asserts
// that the language surface lands on the intended scopes. Run:
//
//   node scripts/grammar-smoke.mjs [--dump <file>]
//
// Exits non-zero when any expected scope is missing, so it doubles as the
// grammar regression check.

import { readFileSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
// Both packages are CommonJS (vscode-oniguruma is a UMD bundle), so they are
// loaded through require() to get their real exports.
const oniguruma = require("vscode-oniguruma");
const textmate = require("vscode-textmate");
const grammarPath = "syntaxes/cp3.tmLanguage.json";

// JSON.parse also proves the grammar file is valid JSON.
const grammar = JSON.parse(readFileSync(grammarPath, "utf8"));
if (grammar.scopeName !== "source.cp3") {
    throw new Error(`unexpected scopeName: ${grammar.scopeName}`);
}

const wasm = readFileSync(require.resolve("vscode-oniguruma/release/onig.wasm"));
await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));

const registry = new textmate.Registry({
    onigLib: Promise.resolve({
        createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
        createOnigString: (s) => new oniguruma.OnigString(s),
    }),
    loadGrammar: async (scopeName) => (scopeName === "source.cp3" ? grammar : null),
});

const loaded = await registry.loadGrammar("source.cp3");
if (loaded === null) {
    throw new Error("grammar did not load");
}

/** Tokenizes a whole document, carrying the rule stack across lines. */
function tokenizeDocument(lines) {
    const tokens = [];
    let stack = textmate.INITIAL;
    for (let lineNumber = 0; lineNumber < lines.length; lineNumber += 1) {
        const line = lines[lineNumber];
        const result = loaded.tokenizeLine(line, stack);
        stack = result.ruleStack;
        for (const token of result.tokens) {
            const text = line.slice(token.startIndex, token.endIndex);
            if (text.trim().length === 0) {
                continue;
            }
            tokens.push({ line: lineNumber + 1, text, scopes: token.scopes.slice() });
        }
    }
    return tokens;
}

const SAMPLE = [
    "// line comment",
    "/* block comment */",
    "import console from \"std/console\";",
    "",
    "interface Logger {",
    "  function log(str: string): void;",
    "}",
    "",
    "class Counter extends Base implements Sized {",
    "  private count: number = 0;",
    "  weak parent: Counter;",
    "  static function increment(): void { Counter.count = Counter.count + 1; }",
    "  get value(): number { return this.count; }",
    "}",
    "",
    "enum Color { Red, Green, Blue }",
    "type Name = string;",
    "",
    "function addTen(ref x: number) {",
    "  x = x + 10;",
    "}",
    "",
    "async function countdown(from: number): Promise<void> {",
    "  let n = from;",
    "  while (n > 0) {",
    "    console.log(`tick ${n} at ${`inner ${n}`}`);",
    "    await sleep(100);",
    "    n = n - 1;",
    "  }",
    "  for (let item of [1, 2]) { print(item); }",
    "  try { risky(); } catch (e) { print(e); } finally { print(\"done\"); }",
    "}",
    "",
    "let e = new EventEmitter();",
    "e.on(\"greet\", (name) => console.log(name));",
    "let flag = true; let nothing = null; let missing = undefined; let inf = Infinity;",
    "let raw: r_'u32' = 5;",
    "let hex = 0x1F; let bin = 0b101; let big = 1_000_000; let f = 3.14e-2; let dot = .5;",
    "let arr = [1, 2, ...rest];",
    "parallel.for(0..arr.length, (i) => { arr[i] = arr[i] * 2; });",
    "setTimeout(() => print(\"later\"), 0);",
];

// The scope names below are the ones VS Code's built-in Dark+ / Light+ themes
// already colour: `entity.name.function`, `entity.name.type`, `entity.name.class`,
// `variable.other.readwrite`, `variable.parameter`, `variable.other.property`,
// `keyword.control`, `storage.type`, `storage.modifier`, `support.class`,
// `support.function`, `support.type`, `variable.language`, `constant.*`,
// `string.*`, `comment.*` and `keyword.operator`. A cp3 file must read exactly
// like the JavaScript/TypeScript file next to it.
const EXPECTATIONS = [
    [" line comment", "comment.line.double-slash.cp3"],
    ["/*", "comment.block.cp3"],
    ["import", "keyword.control.import.cp3"],
    ["std/console", "string.quoted.double.cp3"],
    ["interface", "storage.type.interface.cp3"],
    ["function", "storage.type.function.cp3"],
    ["string", "support.type.primitive.cp3"],
    ["void", "support.type.primitive.cp3"],
    ["class", "storage.type.class.cp3"],
    ["extends", "storage.modifier.extends.cp3"],
    ["implements", "storage.modifier.extends.cp3"],
    ["private", "storage.modifier.cp3"],
    ["number", "support.type.primitive.cp3"],
    ["weak", "storage.modifier.cp3"],
    ["static", "storage.modifier.cp3"],
    ["enum", "storage.type.enum.cp3"],
    ["type", "storage.type.cp3"],
    ["ref", "storage.modifier.cp3"],
    ["async", "storage.modifier.cp3"],
    ["Promise", "entity.name.type.cp3"],
    ["let", "storage.type.cp3"],
    ["while", "keyword.control.cp3"],
    ["console", "support.namespace.cp3"],
    ["console", "support.class.builtin.cp3"],
    ["${", "punctuation.definition.template-expression.begin.cp3"],
    ["tick ", "string.template.cp3"],
    ["inner ", "string.template.cp3"],
    ["await", "keyword.control.cp3"],
    ["sleep", "support.function.builtin.cp3"],
    ["of", "keyword.operator.expression.of.cp3"],
    ["try", "keyword.control.cp3"],
    ["catch", "keyword.control.cp3"],
    ["finally", "keyword.control.cp3"],
    ["new", "keyword.operator.new.cp3"],
    ["EventEmitter", "support.class.builtin.cp3"],
    ["=>", "storage.type.function.arrow.cp3"],
    ["true", "constant.language.cp3"],
    ["null", "constant.language.cp3"],
    ["undefined", "constant.language.cp3"],
    ["Infinity", "constant.language.cp3"],
    ["r_'u32'", "support.type.prefixed.cp3"],
    ["0x1F", "constant.numeric.hex.cp3"],
    ["0b101", "constant.numeric.binary.cp3"],
    ["1_000_000", "constant.numeric.decimal.cp3"],
    ["3.14e-2", "constant.numeric.decimal.cp3"],
    [".5", "constant.numeric.decimal.cp3"],
    ["...", "keyword.operator.spread.cp3"],
    ["parallel", "support.namespace.cp3"],
    ["parallel", "support.class.builtin.cp3"],
    ["..", "keyword.operator.range.cp3"],
    ["setTimeout", "support.function.builtin.cp3"],
    // Declarations, parameters, types and member access: the constructs the
    // grammar used to leave unstyled.
    ["Logger", "entity.name.type.interface.cp3"],
    ["log", "entity.name.function.cp3"],
    ["str", "variable.parameter.cp3"],
    ["Counter", "entity.name.class.cp3"],
    ["Base", "entity.other.inherited-class.cp3"],
    ["Sized", "entity.other.inherited-class.cp3"],
    ["increment", "entity.name.function.cp3"],
    ["value", "entity.name.function.cp3"],
    ["this", "variable.language.this.cp3"],
    ["count", "variable.other.property.cp3"],
    ["n", "variable.other.readwrite.cp3"],
    ["item", "variable.other.readwrite.cp3"],
    ["Color", "entity.name.type.enum.cp3"],
    ["Red", "variable.other.enummember.cp3"],
    ["Name", "entity.name.type.alias.cp3"],
    ["addTen", "entity.name.function.cp3"],
    ["x", "variable.parameter.cp3"],
    ["countdown", "entity.name.function.cp3"],
    ["from", "variable.parameter.cp3"],
    ["risky", "entity.name.function.cp3"],
    ["on", "entity.name.function.cp3"],
    [":", "keyword.operator.type.annotation.cp3"],
];

// Reserved words get their own tiny sample: they are deliberate compile
// errors in cp3, so they do not belong in a realistic document.
const RESERVED = [
    "let a: any = 1;",
    "yield 1;",
    "map.delete(key);",
];
const RESERVED_EXPECTATIONS = [
    ["any", "keyword.reserved.cp3"],
    ["yield", "keyword.reserved.cp3"],
    ["delete", "keyword.reserved.cp3"],
];

const dumpIndex = process.argv.indexOf("--dump");
if (dumpIndex !== -1 && process.argv[dumpIndex + 1] !== undefined) {
    const dumpPath = process.argv[dumpIndex + 1];
    const lines = [];
    for (const token of tokenizeDocument(SAMPLE)) {
        lines.push(`${String(token.line).padStart(3)} ${JSON.stringify(token.text)} ${token.scopes.join(" > ")}`);
    }
    writeFileSync(dumpPath, lines.join("\n") + "\n");
    console.log(`token dump written to ${dumpPath}`);
}

let failures = 0;
function expect(tokens, text, scope) {
    const found = tokens.some((token) => token.text === text && token.scopes.includes(scope));
    const label = `${JSON.stringify(text)} -> ${scope}`;
    if (!found) {
        failures += 1;
        console.log(`FAIL ${label}`);
    } else {
        console.log(`ok   ${label}`);
    }
}

const sampleTokens = tokenizeDocument(SAMPLE);
for (const [text, scope] of EXPECTATIONS) {
    expect(sampleTokens, text, scope);
}

const reservedTokens = tokenizeDocument(RESERVED);
for (const [text, scope] of RESERVED_EXPECTATIONS) {
    expect(reservedTokens, text, scope);
}

console.log(
    failures === 0
        ? `grammar-smoke: all ${EXPECTATIONS.length + RESERVED_EXPECTATIONS.length} checks passed`
        : `grammar-smoke: ${failures} failure(s)`
);
// Set the code and let the process end on its own: calling process.exit here
// can trip a libuv teardown assertion on Node 24 for Windows
// (win/async.c: !(handle->flags & UV_HANDLE_CLOSING)) once the WASM
// oniguruma instance is alive.
process.exitCode = failures === 0 ? 0 : 1;
