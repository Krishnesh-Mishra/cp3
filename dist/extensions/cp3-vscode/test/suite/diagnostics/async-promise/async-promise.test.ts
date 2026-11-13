// Diagnostics / async and Promise errors
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: async and Promise errors", [
    {
        name: "await outside an async function is an error",
        source: "const value = await fetch(\"url\");\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an async function with a wrong declared return type is an error",
        source: "async function f(): Promise<number> {\n  return \"not a number\";\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an async function returning a plain string type is an error",
        source: "async function f(): Promise<string> {\n  return 42;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a Promise of a wrong type assigned to a number const is an error",
        source: "async function produce(): Promise<number> {\n  return 1;\n}\nconst n: number = produce();\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an async function with a correct Promise return type is not an error",
        source: "async function f(): Promise<number> {\n  return 42;\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
]);
