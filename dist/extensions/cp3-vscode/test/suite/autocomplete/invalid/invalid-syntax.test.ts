// Autocomplete / invalid or incomplete syntax
import { completionCases } from "../../harness";

completionCases("Autocomplete: invalid and incomplete syntax", [
    {
        name: "stray dot before cursor is a well-formed empty answer",
        source: "const Obj = { name: \"Rahul\" };\nconst value = Obj.\n__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "dangling dot after expression is a well-formed empty answer",
        source: "const number = 1 + 2;\nnumber.\n__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "unterminated string yields a well-formed empty answer",
        source: "const broken = \"unterminated\n__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "unterminated template literal yields a well-formed empty answer",
        source: "const broken = `never closed\n__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "unterminated block comment yields a well-formed empty answer",
        source: "/* never closed\nconst value = __CURSOR__;\n",
        expectPresent: [],
    },
    {
        name: "line comment with a partial identifier yields nothing useful",
        source: "// the lone identifier: __CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "double dot in a row is answered without crashing",
        source: "const Obj = { name: \"x\" };\nObj..__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "completion at the very start of an empty file is well-formed",
        source: "__CURSOR__",
        expectPresent: [],
    },
    {
        name: "completion after an unmatched paren is well-formed",
        source: "function add(a: number, b: number): number { return a + b; }\nconst total = add(1,\n__CURSOR__\n",
        expectPresent: [],
    },
    {
        name: "type annotation missing its type asks for nothing illegal",
        source: "function broken(n: number, ): void {\n  const value = __CURSOR__;\n}\n",
        expectPresent: ["n"],
    },
]);
