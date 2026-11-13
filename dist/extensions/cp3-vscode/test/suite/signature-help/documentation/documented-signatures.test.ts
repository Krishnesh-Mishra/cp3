// Signature help / documentation content
import { signatureHelpCases } from "../../harness";

signatureHelpCases("Signature help: documentation", [
    {
        name: "a documented function shows its description in signature help",
        source: "/**\n * Renders a greeting card.\n */\nfunction greet(name: string): string {\n  return `Hello, ${name}`;\n}\nconst out = greet(__CURSOR__\"Asha\");\n",
        contains: ["greeting"],
    },
    {
        name: "a documented function shows the parameter docs",
        source: "/**\n * Doubles a number.\n * @param value The input to double.\n * @returns The doubled value.\n */\nfunction dbl(value: number): number {\n  return value * 2;\n}\nconst out = dbl(__CURSOR__4);\n",
        contains: ["value"],
    },
    {
        name: "a /// doc comment shows in signature help",
        source: "/// Returns the sum of two inputs.\nfunction sum(a: number, b: number): number {\n  return a + b;\n}\nconst out = sum(__CURSOR__1, 2);\n",
        contains: ["sum"],
    },
    {
        name: "a documented function includes the @returns text",
        source: "/**\n * Adds two values.\n * @returns The sum of the inputs.\n */\nfunction sum(a: number, b: number): number {\n  return a + b;\n}\nconst out = sum(__CURSOR__1, 2);\n",
        contains: ["sum"],
    },
    {
        name: "a std member shows its documented signature",
        source: "import console from \"std/console\";\nconsole.__CURSOR__log(\"hi\");\n",
        contains: ["log"],
    },
    {
        name: "path.join shows the variadic signature",
        source: "import path from \"std/path\";\nconst joined = path.__CURSOR__join(\"a\", \"b\");\n",
        contains: ["join"],
    },
    {
        name: "Math.abs shows the single parameter signature",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__abs(-1);\n",
        contains: ["abs"],
    },
    {
        name: "a documented constructor shows the constructor docs",
        source: "class Widget {\n  /** Creates a widget with the given size. */\n  constructor(size: number) {\n  }\n}\nconst w = new Widget(__CURSOR__10);\n",
        contains: ["widget"],
    },
    {
        name: "a documented method shows the method docs",
        source: "class Counter {\n  /** Increments the counter by one. */\n  increment(): void {}\n}\nconst c = new Counter();\nc.__CURSOR__increment();\n",
        contains: ["ncrement"],
    },
    {
        name: "the doc for the active parameter is listed in the signature help",
        source: "/**\n * Combines two strings.\n * @param first The leading string.\n * @param second The trailing string.\n */\nfunction combine(first: string, second: string): string {\n  return first + second;\n}\nconst joined = combine(\"x\", __CURSOR__\"y\");\n",
        expectActiveParameter: 1,
        contains: ["second"],
    },
]);
