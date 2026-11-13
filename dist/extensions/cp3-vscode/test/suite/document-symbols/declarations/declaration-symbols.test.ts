// Document symbols / declarations across kinds
import { documentSymbolCases } from "../../harness";

documentSymbolCases("Document symbols: declarations", [
    {
        name: "symbol table lists a function",
        source: "function helper(): number {\n  return 1;\n}\n",
        contains: ["helper"],
    },
    {
        name: "symbol table lists an exported function",
        source: "export function exportedFn(): number {\n  return 1;\n}\n",
        contains: ["exportedFn"],
    },
    {
        name: "symbol table lists a const",
        source: "const shared = 41;\n",
        contains: ["shared"],
    },
    {
        name: "symbol table lists a let",
        source: "let counter = 0;\n",
        contains: ["counter"],
    },
    {
        name: "symbol table lists a var",
        source: "var legacy = 1;\n",
        contains: ["legacy"],
    },
    {
        name: "symbol table lists a class",
        source: "class Machine {}\n",
        contains: ["Machine"],
    },
    {
        name: "symbol table lists an interface",
        source: "interface Sized {\n  size(): number;\n}\n",
        contains: ["Sized"],
    },
    {
        name: "symbol table lists an enum",
        source: "enum Direction { North, South }\n",
        contains: ["Direction"],
    },
    {
        name: "symbol table lists a type alias",
        source: "type Radians = number;\n",
        contains: ["Radians"],
    },
    {
        name: "symbol table lists a generic function",
        source: "function identity<T>(value: T): T {\n  return value;\n}\n",
        contains: ["identity"],
    },
    {
        name: "symbol table lists an async function",
        source: "async function pending(): Promise<number> {\n  return 1;\n}\n",
        contains: ["pending"],
    },
    {
        name: "symbol table lists a default exported class",
        source: "export default class Widget {}\n",
        contains: ["Widget"],
    },
    {
        name: "symbol table lists a default exported function",
        source: "export default function helper(): number {\n  return 1;\n}\n",
        contains: ["helper"],
    },
    {
        name: "symbol table lists an enum member",
        source: "enum Direction { North, South }\n",
        contains: ["Direction"],
    },
    {
        name: "symbol table lists a nested module-level arrow const",
        source: "const dbl = (n: number): number => n * 2;\n",
        contains: ["dbl"],
    },
]);
