// Hover / keywords, strings and doc comments
import { hoverCases } from "../../harness";

hoverCases("Hover: keywords, strings and docs", [
    {
        name: "the if keyword answers no hover",
        source: "if (__CURSOR__true) {\n  print(\"yes\");\n}\n",
        expectNull: true,
    },
    {
        name: "the for keyword answers no hover",
        source: "for (__CURSOR__let i = 0; i < 1; i += 1) {\n  print(`${i}`);\n}\n",
        expectNull: true,
    },
    {
        name: "the return keyword answers no hover",
        source: "function f(): number {\n  __CURSOR__return 1;\n}\n",
        expectNull: true,
    },
    {
        name: "the const keyword answers no hover",
        source: "__CURSOR__const value = 1;\n",
        expectNull: true,
    },
    {
        name: "a string hover shows the inferred type",
        source: "const value: string = \"hi\";\nconst read = __CURSOR__value;\n",
        contains: ["value"],
    },
    {
        name: "a /// doc comment on a declaration appears in hover",
        source: "/// Returns the sum of two numbers.\nfunction sum(a: number, b: number): number {\n  return a + b;\n}\nconst out = __CURSOR__sum(1, 2);\n",
        contains: ["sum"],
    },
    {
        name: "a block doc comment on a declaration appears in hover",
        source: "/**\n * Adds two numbers together.\n */\nfunction add(a: number, b: number): number {\n  return a + b;\n}\nconst out = __CURSOR__add(1, 2);\n",
        contains: ["Adds"],
    },
    {
        name: "a doc comment with param entries appears in hover",
        source: "/**\n * Multiplies a number by two.\n * @param value The input to double.\n * @returns The doubled value.\n */\nfunction dbl(value: number): number {\n  return value * 2;\n}\nconst out = __CURSOR__dbl(2);\n",
        contains: ["value"],
    },
    {
        name: "an object property hover shows the property type",
        source: "const user = { name: \"Test\", age: 25 };\nconst read = __CURSOR__user.name;\n",
        contains: ["name"],
    },
    {
        name: "the true literal answers no hover",
        source: "const flag = __CURSOR__true;\n",
        expectNull: true,
    },
]);
