// Diagnostics / invalid function arguments
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: invalid function arguments", [
    {
        name: "a parallel.run closure that takes parameters is rejected",
        source: "import console from \"std/console\";\n\nlet results = parallel.run((value: number) => value);\nconsole.log(`${results[0]}`);\n",
        expect: [{ severity: "error", code: "CP3912", line: 3, contains: "takes no parameters" }],
    },
    {
        name: "too few arguments to a two-parameter add is an error",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "too many arguments to a two-parameter add is an error",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, 2, 3);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "string passed where a number parameter is expected is an error",
        source: "function f(n: number): void {}\nf(\"one\");\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "number passed where a string parameter is expected is an error",
        source: "function f(s: string): void {}\nf(1);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "boolean passed where a string parameter is expected is an error",
        source: "function f(s: string): void {}\nf(true);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "null passed where a string parameter is expected is an error",
        source: "function f(s: string): void {}\nf(null);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "undefined passed where a string parameter is expected is an error",
        source: "function f(s: string): void {}\nf(undefined);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "wrong object shape for a parameter is an error",
        source: "interface Point { x: number; y: number; }\nfunction place(p: Point): void {}\nplace({ x: 1 });\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "calling a non-function is an error",
        source: "const notCallable = 42;\nnotCallable();\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "array literal where number parameter is expected is an error",
        source: "function f(n: number): void {}\nf([1, 2, 3]);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "function reference where a number parameter is expected is an error",
        source: "function f(n: number): void {}\nfunction g(): number { return 1; }\nf(g);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "console.log of a string is a valid program with no diagnostics",
        source: "import console from \"std/console\";\nconsole.log(\"x\");\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "extra argument in a nested call is an error",
        source: "function inner(pair: string): string { return pair; }\nfunction outer(a: string, b: string): string {\n  return inner(a, b);\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "mismatched tuple-like argument is an error",
        source: "function combine(a: string, b: number): string { return a; }\nconst r = combine(\"x\", \"y\");\n",
        expect: [{ severity: "error" }],
    },
]);
