// Go to definition / variables
import { definitionCases } from "../../harness";

definitionCases("Go to definition: variables", [
    {
        name: "a const declared above jumps to its declaration",
        source: "const shared = 41;\nconst value = __CURSOR__shared + 1;\n",
        expectLines: [0],
    },
    {
        name: "a let declared above jumps to its declaration",
        source: "let count = 0;\ncount__CURSOR__;\n",
        expectLines: [0],
    },
    {
        name: "a local const inside a function jumps to its declaration",
        source: "function main(): number {\n  const total = 10;\n  return __CURSOR__total;\n}\n",
        expectLines: [1],
    },
    {
        name: "a parameter jumps to the parameter declaration",
        source: "function greet(name: string): string {\n  return __CURSOR__name;\n}\n",
        expectLines: [0],
    },
    {
        name: "a module-level const used inside a function jumps out of the function",
        source: "const GLOBAL = \"g\";\nfunction read(): string {\n  return __CURSOR__GLOBAL;\n}\n",
        expectLines: [0],
    },
    {
        name: "a shadowed variable jumps to the nearest declaration",
        source: "const count = 100;\nfunction main(): number {\n  const count = 1;\n  return __CURSOR__count;\n}\n",
        expectLines: [2],
    },
    {
        name: "a for-loop variable jumps to the loop declaration",
        source: "function main(): number {\n  for (let index = 0; index < 3; index += 1) {\n    print(__CURSOR__index);\n  }\n  return 0;\n}\n",
        expectLines: [1],
    },
    {
        name: "a variable in the middle of a function is found from the top",
        source: "const alpha = \"a\";\nconst beta = __CURSOR__alpha;\nconst gamma = beta;\n",
        expectLines: [0],
    },
    {
        name: "a variable used as a function argument jumps to its declaration",
        source: "const seed = 1;\nfunction bump(n: number): number { return n + 1; }\nconst out = bump(__CURSOR__seed);\n",
        expectLines: [0],
    },
    {
        name: "the same variable name twice jumps to the nearest one",
        source: "const item = \"outer\";\nfunction main(): string {\n  const item = \"inner\";\n  return __CURSOR__item;\n}\n",
        expectLines: [2],
    },
]);
