// Diagnostics / syntax errors
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: syntax errors", [
    {
        name: "stray dot after expression is an error",
        source: "const Obj = { name: \"Rahul\" };\nconst x = Obj.\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unterminated string literal is an error",
        source: "const broken = \"never closed\nconst after = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unterminated template literal is an error",
        source: "const broken = `never closed\nconst after = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unterminated block comment is an error or recovers gracefully",
        source: "/* never closed\nconst after = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unmatched open paren is an error",
        source: "function f(a: number {\n  return a;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unmatched open brace is an error",
        source: "function f(): void {\n  if (true) {\n    return;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "unmatched open bracket is an error",
        source: "const xs = [1, 2, 3;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "double dot in a row is an error",
        source: "const Obj = { name: \"x\" };\nObj..name\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "missing closing brace in a function is an error",
        source: "function f(): number {\n  const x = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "invalid statement keyword 'any' is an error",
        source: "any broken = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "yield outside a generator is an error",
        source: "yield 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "delete used as a keyword is an error",
        source: "map.delete(key);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "empty import specifier is an error",
        source: "import {} from \"std/math\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "arrow function with no body and no return type is an error",
        source: "const f = => 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "duplicate export of the same name is an error",
        source: "const dup = 1;\nconst dup = 2;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "expression statement missing its semicolon after a return is an error",
        source: "function f(): number {\n  return 1\n  + 2;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a dangling operator at end of line is an error",
        source: "const x = 1 +\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an unclosed generic parameter list is an error",
        source: "function f<T(x: T): T {\n  return x;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a class without a name is rejected",
        source: "class {\n  value: number = 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a stray semicolon where a statement cannot appear is an error",
        source: "const ok = 1;\n);\n",
        expect: [{ severity: "error" }],
    },
]);
