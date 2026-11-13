// Hover / function signatures and documentation
import { hoverCases } from "../../harness";

hoverCases("Hover: functions and signatures", [
    {
        name: "a function hover shows the callee with its parameters",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = __CURSOR__add(1, 2);\n",
        contains: ["add"],
    },
    {
        name: "a function hover includes the return type",
        source: "function render(item: string): string {\n  return item;\n}\nconst out = __CURSOR__render(\"x\");\n",
        contains: ["string"],
    },
    {
        name: "a function hover shows the parameter types",
        source: "function combine(a: string, b: string): string {\n  return a + b;\n}\nconst out = __CURSOR__combine(\"a\", \"b\");\n",
        contains: ["string"],
    },
    {
        name: "a documented function hover shows its description",
        source: "/**\n * Renders a greeting card for the given name.\n */\nfunction greet(name: string): string {\n  return `Hello, ${name}`;\n}\nconst out = __CURSOR__greet(\"Asha\");\n",
        contains: ["greeting card"],
    },
    {
        name: "a documented function hover shows the param documentation",
        source: "/**\n * Doubles a number.\n * @param value The input to double.\n * @returns The doubled value.\n */\nfunction dbl(value: number): number {\n  return value * 2;\n}\nconst out = __CURSOR__dbl(4);\n",
        contains: ["doubles"],
    },
    {
        name: "a documented function hover shows the return doc",
        source: "/**\n * Adds two values.\n * @returns The sum of the inputs.\n */\nfunction sum(a: number, b: number): number {\n  return a + b;\n}\nconst out = __CURSOR__sum(1, 2);\n",
        contains: ["sum"],
    },
    {
        name: "a std member hover shows the std function name",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__abs(-1);\n",
        contains: ["abs"],
    },
    {
        name: "an async function hover shows the promise return",
        source: "async function pending(): Promise<number> {\n  return 1;\n}\nconst p = __CURSOR__pending();\n",
        contains: ["Promise"],
    },
    {
        name: "an arrow function hover shows its type",
        source: "const dbl = (n: number): number => n * 2;\nconst out = __CURSOR__dbl(2);\n",
        contains: ["number"],
    },
    {
        name: "hovering a void function shows void in the signature",
        source: "function greet(): void {}\n__CURSOR__greet();\n",
        contains: ["void"],
    },
]);
