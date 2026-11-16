// Autocomplete / type completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: type completion", [
    {
        name: "type annotation offers number",
        source: "function f(n: __CURSOR__): undefined {}\n",
        expectPresent: ["number"],
    },
    {
        name: "type annotation offers string",
        source: "function f(n: __CURSOR__): undefined {}\n",
        expectPresent: ["string"],
    },
    {
        name: "type annotation offers boolean",
        source: "function f(n: __CURSOR__): undefined {}\n",
        expectPresent: ["boolean"],
    },
    {
        name: "type annotation offers undefined in return position",
        source: "function f(): __CURSOR__ {\n  return;\n}\n",
        expectPresent: ["undefined"],
    },
    {
        name: "type annotation offers the interface declared in the file",
        source: "interface Config { retries: number; }\nfunction load(c: __CURSOR__): undefined {}\n",
        expectPresent: ["Config"],
    },
    {
        name: "type annotation offers the class declared in the file",
        source: "class Store {}\nfunction attach(store: __CURSOR__): undefined {}\n",
        expectPresent: ["Store"],
    },
    {
        name: "type annotation offers the enum declared in the file",
        source: "enum Mode { Fast, Slow }\nfunction run(mode: __CURSOR__): undefined {}\n",
        expectPresent: ["Mode"],
    },
    {
        name: "type annotation offers the type alias declared in the file",
        source: "type Pixels = number;\nfunction paint(p: __CURSOR__): undefined {}\n",
        expectPresent: ["Pixels"],
    },
    {
        name: "array element type annotation offers primitives",
        source: "function f(a: Array<__CURSOR__>): undefined {}\n",
        expectPresent: ["number", "string"],
    },
    {
        name: "Promise type argument offers undefined",
        source: "async function f(): Promise<__CURSOR__> {\n  return;\n}\n",
        expectPresent: ["undefined"],
    },
    {
        name: "Promise type argument offers number",
        source: "async function f(): Promise<__CURSOR__> {\n  return 1;\n}\n",
        expectPresent: ["number"],
    },
    {
        name: "type position after let annotation offers primitives",
        source: "const count: __CURSOR__ = 1;\n",
        expectPresent: ["number"],
    },
    {
        name: "type position does not offer value-only builtins without a type meaning",
        source: "function f(n: __CURSOR__): undefined {}\n",
        expectAbsent: ["finally", "catch", "return", "import"],
    },
    {
        name: "interface implementation list offers interfaces only",
        source: "interface Drawable { draw(): undefined; }\nclass Square implements __CURSOR__ {\n  draw(): undefined {}\n}\n",
        expectPresent: ["Drawable"],
    },
    {
        name: "extends clause offers classes",
        source: "class Base {}\nclass Derived extends __CURSOR__ {}\n",
        expectPresent: ["Base"],
    },
]);

