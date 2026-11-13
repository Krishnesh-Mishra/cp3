// Autocomplete / keyword completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: keyword completion", [
    {
        name: "statement position offers let and const",
        source: "function main(): void {\n  __CURSOR__\n}\n",
        expectPresent: ["let", "const"],
    },
    {
        name: "statement position offers control flow",
        source: "function main(): void {\n  __CURSOR__\n}\n",
        expectPresent: ["if", "for", "while", "return"],
    },
    {
        name: "statement position offers function and class",
        source: "__CURSOR__\n",
        expectPresent: ["function", "class", "interface", "enum"],
    },
    {
        name: "top level offers import",
        source: "__CURSOR__\n",
        expectPresent: ["import", "export"],
    },
    {
        name: "inside a try block offers catch and finally",
        source: "function main(): void {\n  try {\n    risky();\n    __CURSOR__\n  } catch (e) {\n  }\n}\n",
        expectPresent: ["catch", "finally"],
    },
    {
        name: "inside a class body offers member modifiers and function",
        source: "class Machine {\n  __CURSOR__\n}\n",
        expectPresent: ["private", "static", "function"],
    },
    {
        name: "async function body offers await",
        source: "async function main(): Promise<void> {\n  __CURSOR__\n}\n",
        expectPresent: ["await"],
    },
    {
        name: "parameter list offers ref",
        source: "function bump(__CURSOR__x: number): void {}\n",
        expectPresent: ["ref"],
    },
    {
        name: "type position offers primitive types but not statement keywords",
        source: "function id(value: __CURSOR__): number { return 0; }\n",
        expectPresent: ["number", "string"],
        expectAbsent: ["finally", "catch", "implements", "private"],
    },
    {
        name: "expression position does not offer illegal statement keywords",
        source: "const value = __CURSOR__;\n",
        expectAbsent: ["finally", "catch", "implements", "private", "abstract"],
    },
]);
