// Diagnostics / missing properties
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: missing properties", [
    {
        name: "writing to an unknown member of an object literal is an error",
        source: "import console from \"std/console\";\nlet o = { a: 1 };\no.b = 2;\nconsole.log(`${o.a}`);\n",
        expect: [{ severity: "error", code: "CP3305", line: 3, contains: "no member 'b'" }],
    },
    {
        name: "reading an unknown member of an object literal is an error",
        source: "const o = { a: 1 };\nconst b = o.b;\n",
        expect: [{ severity: "error", contains: "b" }],
    },
    {
        name: "missing member of an interface-typed object is an error",
        source: "interface Sized {\n  length: number;\n  capacity: number;\n}\nconst s: Sized = { length: 1 };\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "missing member of an interface-typed object reports at the literal",
        source: "interface Point {\n  x: number;\n  y: number;\n}\nconst p: Point = { x: 1 };\n",
        expect: [{ severity: "error", line: 4 }],
    },
    {
        name: "deep missing property is an error on the unknown member",
        source: "const state = { level: { depth: 1 } };\nconst name = state.levels.count;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "missing property in an interface implementation is an error",
        source: "interface Pair {\n  left: number;\n  right: number;\n}\nclass Impl implements Pair {\n  left: number = 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "object member on an untyped empty object is an error",
        source: "const o = {};\no.anything = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "missing method of an interface-typed parameter is an error",
        source: "interface Storage {\n  load(): string;\n  save(s: string): void;\n}\nconst s: Storage = { load(): string { return \"x\"; } };\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "missing property passed as an object argument is an error",
        source: "interface Config {\n  retries: number;\n  timeout: number;\n}\nfunction run(c: Config): void {}\nrun({ retries: 1 });\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "valid object literal against a complete interface is not an error",
        source: "import console from \"std/console\";\ninterface Sized {\n  length: number;\n}\nconst s: Sized = { length: 1 };\nconsole.log(`${s.length}`);\n",
        expect: [],
        expectNoDiagnostics: true,
    },
]);
