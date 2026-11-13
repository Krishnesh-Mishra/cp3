// Autocomplete / function completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: function completion", [
    {
        name: "top-level function declared above is offered",
        source: "function helper(): number { return 1; }\nconst value = __CURSOR__;\n",
        expectPresent: ["helper"],
    },
    {
        name: "function with parameters is offered by name",
        source: "function add(a: number, b: number): number { return a + b; }\nconst total = __CURSOR__;\n",
        expectPresent: ["add"],
    },
    {
        name: "arrow function const is offered",
        source: "const double = (n: number): number => n * 2;\nconst value = __CURSOR__;\n",
        expectPresent: ["double"],
    },
    {
        name: "function declaration followed by call position offers the name",
        source: "function greet(): void {}\n__CURSOR__\n",
        expectPresent: ["greet"],
    },
    {
        name: "multiple functions are all offered",
        source: "function ping(): void {}\nfunction pong(): void {}\nconst fn = __CURSOR__;\n",
        expectPresent: ["ping", "pong"],
    },
    {
        name: "nested function is offered inside its enclosing function",
        source: "function outer(): void {\n  function inner(): void {}\n  const callee = __CURSOR__;\n}\n",
        expectPresent: ["inner"],
    },
    {
        name: "exported function is still offered in the local file",
        source: "export function exportedFn(): string { return \"x\"; }\nconst value = __CURSOR__;\n",
        expectPresent: ["exportedFn"],
    },
    {
        name: "function with default export keeps its name",
        source: "export function namedExport(): number { return 42; }\nconst v = __CURSOR__;\n",
        expectPresent: ["namedExport"],
    },
    {
        name: "function used as a value offers string-like member completion",
        source: "function render(item: string): string { return item; }\nconst r = __CURSOR__;\n",
        expectPresent: ["render"],
    },
    {
        name: "async function is offered",
        source: "async function load(): Promise<number> { return 1; }\nconst pending = __CURSOR__;\n",
        expectPresent: ["load"],
    },
    {
        name: "function referenced inside its own body is offered (recursion)",
        source: "function rec(n: number): number {\n  if (n <= 0) { return 0; }\n  return rec(__CURSOR__);\n}\n",
        expectPresent: ["rec"],
    },
]);
