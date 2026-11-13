// Document formatting / more verified exact outputs (cp3c fmt)
import { formattingCases } from "../../harness";

formattingCases("Document formatting: structural cases", [
    {
        name: "arrow function body normalizes to 2-space",
        source: "const dbl = (n: number): number => {\n    return n * 2;\n}\n",
        expectText: "const dbl = (n: number): number => {\n  return n * 2;\n}\n",
    },
    {
        name: "a blank line between functions is kept",
        source: "function a(): number {\n  return 1;\n}\n\nfunction b(): number {\n  return 2;\n}\n",
        expectText: "function a(): number {\n  return 1;\n}\n\nfunction b(): number {\n  return 2;\n}\n",
    },
    {
        name: "default parameter lists are left alone",
        source: "function connect(host: string, port: number = 8080): void {\n}\n",
        expectText: "function connect(host: string, port: number = 8080): void {\n}\n",
    },
    {
        name: "an empty function body does not gain content",
        source: "function noop(): void {\n}\n",
        expectText: "function noop(): void {\n}\n",
    },
    {
        name: "multiline call arguments are not re-indented",
        source: "const out = compute(\n    first,\n    second,\n    third\n);\n",
        expectText: "const out = compute(\nfirst,\nsecond,\nthird\n);\n",
    },
    {
        name: "an early return inside an if stays indented at two spaces",
        source: "function f(n: number): number {\n  if (n > 10) {\n    return 1;\n  }\n  return 0;\n}\n",
        expectText: "function f(n: number): number {\n  if (n > 10) {\n    return 1;\n  }\n  return 0;\n}\n",
    },
    {
        name: "a while loop body gets 2-space indentation",
        source: "function f(): number {\n  let n = 3;\n  while (n > 0) {\n    n = n - 1;\n  }\n  return n;\n}\n",
        expectText: "function f(): number {\n  let n = 3;\n  while (n > 0) {\n    n = n - 1;\n  }\n  return n;\n}\n",
    },
    {
        name: "import statements from different modules stay adjacent",
        source: "import Math from 'std/math';\nimport console from 'std/console';\n\nconst value = Math.abs(-1);\n",
        expectText: "import Math from 'std/math';\nimport console from 'std/console';\n\nconst value = Math.abs(-1);\n",
    },
    {
        name: "a nested object literal gets 2-space indentation",
        source: "const point = {\n  origin: { x: 0, y: 0 },\n  scale: 2,\n};\n",
        expectText: "const point = {\n  origin: { x: 0, y: 0 },\n  scale: 2,\n};\n",
    },
    {
        name: "a long member chain stays on one line",
        source: "const rs = [3, 1, 2].map((n) => n * 2).filter((n) => n > 2);\n",
        expectText: "const rs = [3, 1, 2].map((n) => n * 2).filter((n) => n > 2);\n",
    },
]);
