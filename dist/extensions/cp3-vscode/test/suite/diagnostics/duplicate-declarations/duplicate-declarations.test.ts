// Diagnostics / duplicate declarations
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: duplicate declarations", [
    {
        name: "two const declarations with the same name in one scope are an error",
        source: "const value = 1;\nconst value = 2;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "two function declarations with the same name are an error",
        source: "function dup(): number {\n  return 1;\n}\nfunction dup(): number {\n  return 2;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "two class declarations with the same name are an error",
        source: "class Twin {}\nclass Twin {}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "two interface declarations with the same name are an error",
        source: "interface Twin {\n  a: number;\n}\ninterface Twin {\n  b: number;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "two enum declarations with the same name are an error",
        source: "enum Color { Red }\nenum Color { Blue }\n",
        expect: [{ severity: "error" }],
    },
]);
