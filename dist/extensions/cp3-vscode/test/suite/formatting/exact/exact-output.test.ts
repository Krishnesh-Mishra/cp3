// Document formatting / verified exact outputs (cp3c fmt, 2-space indent)
import { formattingCases } from "../../harness";

formattingCases("Document formatting: verified exact outputs", [
    {
        name: "mixed indentation normalizes to 2-space",
        source: "function f(): number {\n  const x = 1;\n        return x;\n}\n",
        expectText: "function f(): number {\n  const x = 1;\n  return x;\n}\n",
    },
    {
        name: "already formatted source is left unchanged",
        source: "function f(): number {\n  const x = 1;\n  return x;\n}\n",
        expectText: "function f(): number {\n  const x = 1;\n  return x;\n}\n",
    },
    {
        name: "nested blocks get 2-space indentation per level",
        source: "function f(): number {\n  if (true) {\n    for (let i = 0; i < 2; i += 1) {\n      const y = i * 2;\n          print(y);\n    }\n  }\n  return 0;\n}\n",
        expectText: "function f(): number {\n  if (true) {\n    for (let i = 0; i < 2; i += 1) {\n      const y = i * 2;\n      print(y);\n    }\n  }\n  return 0;\n}\n",
    },
    {
        name: "class members get 2-space indentation",
        source: "class Counter {\n    count: number = 0;\n\n  increment(): void {\n    this.count = this.count + 1;\n  }\n}\n",
        expectText: "class Counter {\n  count: number = 0;\n\n  increment(): void {\n    this.count = this.count + 1;\n  }\n}\n",
    },
    {
        name: "enum members get 2-space indentation",
        source: "enum Color {\n  Red,\n  Green,\n  Blue,\n}\n",
        expectText: "enum Color {\n  Red,\n  Green,\n  Blue,\n}\n",
    },
    {
        name: "multiline object literal gets 2-space indentation",
        source: "const point = {\n    x: 1,\n    y: 2,\n};\n",
        expectText: "const point = {\n  x: 1,\n  y: 2,\n};\n",
    },
    {
        name: "class method body gets 2-space indentation",
        source: "class Machine {\n  speed(): number {\n    return 4;\n  }\n}\n",
        expectText: "class Machine {\n  speed(): number {\n    return 4;\n  }\n}\n",
    },
    {
        name: "single-quoted strings are preserved verbatim",
        source: "const text = 'hello';\nconst up = text.toUpperCase();\n",
        expectText: "const text = 'hello';\nconst up = text.toUpperCase();\n",
    },
    {
        name: "interface members get 2-space indentation",
        source: "interface Sized {\n      length: number;\n}\n",
        expectText: "interface Sized {\n  length: number;\n}\n",
    },
    {
        name: "a line comment above a declaration is kept",
        source: "// keep this header\nconst value = 1;\n",
        expectText: "// keep this header\nconst value = 1;\n",
    },
]);
