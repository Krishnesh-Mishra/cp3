// Autocomplete / context-aware suggestions
import { completionCases } from "../../harness";

completionCases("Autocomplete: context-aware suggestions", [
    {
        name: "inside a try block offers catch and finally",
        source: "function main(): void {\n  try {\n    risky();\n    __CURSOR__\n  } catch (e) {\n  }\n}\n",
        expectPresent: ["catch", "finally"],
    },
    {
        name: "outside a try block does not offer catch or finally",
        source: "function main(): void {\n  if (true) {\n    __CURSOR__\n  }\n}\n",
        expectAbsent: ["catch", "finally"],
    },
    {
        name: "inside a class body offers member modifiers",
        source: "class Machine {\n  __CURSOR__\n}\n",
        expectPresent: ["private", "static"],
    },
    {
        name: "inside a class body does not offer statement keywords",
        source: "class Machine {\n  __CURSOR__\n}\n",
        expectAbsent: ["return", "while", "catch", "import", "finally"],
    },
    {
        name: "parameter list offers ref and no statement keyword",
        source: "function bump(__CURSOR__x: number): void {}\n",
        expectPresent: ["ref"],
        expectAbsent: ["return", "const", "if", "finally", "catch"],
    },
    {
        name: "a partial statement keeps return and const suggestions",
        source: "function main(): number {\n  const ke__CURSOR__\n}\n",
        expectPresent: ["return"],
        expectAbsent: ["finally", "catch", "implements"],
    },
    {
        name: "interface body offers member declaration modifiers or function",
        source: "interface Shape {\n  __CURSOR__\n}\n",
        expectPresent: ["function"],
    },
    {
        name: "type position in a let annotation offers primitives only",
        source: "const item: __CURSOR__ = \"x\";\n",
        expectPresent: ["string", "number"],
        expectAbsent: ["private", "static", "finally", "catch"],
    },
    {
        name: "import statement value position offers nothing illegal",
        source: "import __CURSOR__ from \"std/math\";\n",
        expectAbsent: ["catch", "finally", "private", "static"],
    },
    {
        name: "template literal interpolation returns to expression position",
        source: "const n = 41;\nconst label = `value: ${__CURSOR__}`;\n",
        expectPresent: ["n"],
    },
    {
        name: "after new offers classes and builtins",
        source: "class Widget {}\nconst w = new __CURSOR__();\n",
        expectPresent: ["Widget"],
    },
    {
        name: "export list position offers local declarations",
        source: "const internal = 1;\nfunction helper(): number { return internal; }\nexport { __CURSOR__ };\n",
        expectPresent: ["helper", "internal"],
    },
    {
        name: "enum body offers constant declarations context",
        source: "enum Color {\n  Red,\n  __CURSOR__\n}\n",
        expectAbsent: ["finally", "catch", "private"],
    },
    {
        name: "callback argument position offers surrounding names",
        source: "function each(items: number[], cb: (item: number) => void): void {}\neach([1], (item) => {\n  const inner = item + 1;\n  each([inner], (next) => {\n    print(__CURSOR__);\n  });\n});\n",
        expectPresent: ["inner", "next"],
    },
    {
        name: "dangling dot in a broken file is a well-formed empty answer",
        source: "const Obj = { name: \"Rahul\" };\nconst x = Obj.\n__CURSOR__\n",
        expectPresent: [],
        expectAbsent: ["catch", "finally"],
    },
]);
