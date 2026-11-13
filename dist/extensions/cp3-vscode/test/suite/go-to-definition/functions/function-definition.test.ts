// Go to definition / functions and imports
import { definitionCases } from "../../harness";

definitionCases("Go to definition: functions", [
    {
        name: "a call jumps to the same-file function declaration",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = __CURSOR__add(1, 2);\n",
        expectLines: [0],
    },
    {
        name: "a call inside a function jumps to the declaration above it",
        source: "function helper(): number { return 1; }\nfunction main(): number {\n  return __CURSOR__helper();\n}\n",
        expectLines: [0],
    },
    {
        name: "a nested function jumps to its declaration",
        source: "function outer(): void {\n  function inner(): void {}\n  __CURSOR__inner();\n}\n",
        expectLines: [1],
    },
    {
        name: "an async function jumps to its declaration",
        source: "async function pending(): Promise<number> {\n  return 1;\n}\nconst p = __CURSOR__pending();\n",
        expectLines: [0],
    },
    {
        name: "an arrow function const jumps to the const declaration",
        source: "const dbl = (n: number): number => n * 2;\nconst out = __CURSOR__dbl(4);\n",
        expectLines: [0],
    },
    {
        name: "an exported function used locally jumps to its declaration",
        source: "export function exportedFn(): number { return 1; }\nconst use = __CURSOR__exportedFn();\n",
        expectLines: [0],
    },
    {
        name: "a std import answers no definition inside the std module",
        source: "import Math from \"std/math\";\nconst x = __CURSOR__Math.abs(-1);\n",
        expectNone: true,
    },
    {
        name: "an imported local function jumps into the defining module",
        source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a default-exported function jumps into the defining module",
        source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export default function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a re-exported function jumps through the barrel to the source",
        source: "import { helper } from \"./barrel.cp3\";\nconst out = __CURSOR__helper();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function helper(): number { return 1; }\n"]],
        expectLines: [0],
    },
]);
