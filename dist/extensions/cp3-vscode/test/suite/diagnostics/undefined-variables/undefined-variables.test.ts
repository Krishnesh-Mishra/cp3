// Diagnostics / undefined variables
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: undefined variables", [
    {
        name: "cannot find name suggests the declared message variable",
        source: "import console from \"std/console\";\nlet message = \"hi\";\nconsole.log(massage);\n",
        expect: [{ severity: "error", code: "CP3301", line: 3, contains: "did you mean 'message'" }],
    },
    {
        name: "bare Math without import is an error suggesting the import line",
        source: "let x = Math.random();\n",
        expect: [{ severity: "error", code: "CP3301", line: 1, contains: "std/math" }],
    },
    {
        name: "undefined name gives an error on the use line",
        source: "const total = nonexistentName + 1;\n",
        expect: [{ severity: "error", code: "CP3301", line: 1, contains: "nonexistentName" }],
    },
    {
        name: "uncaught binding reference suggests the right spelling",
        source: "let message = \"hello\";\nconst other = mesage;\n",
        expect: [{ severity: "error", code: "CP3301", line: 2, contains: "did you mean 'message'" }],
    },
    {
        name: "e outside the catch block cannot be found",
        source: "import console from \"std/console\";\ntry { throw new Error(\"x\"); } catch (e) { console.log(\"in\"); }\nconsole.log(`${e}`);\n",
        expect: [{ severity: "error", code: "CP3301", line: 3, contains: "cannot find name 'e'" }],
    },
    {
        name: "undefined variable in a return position is an error",
        source: "function read(): number {\n  return missing;\n}\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "undefined variable in a function argument is an error",
        source: "function bump(n: number): void {}\nbump(nope);\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "undefined variable in a condition is an error",
        source: "if (missing) {\n  print(\"yes\");\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "using a variable before its declaration is an error",
        source: "function main(): number {\n  return later;\n  const later = 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "undefined property path errors on the unknown member",
        source: "import console from \"std/console\";\nlet o = { a: 1 };\no.notathing = 2;\n",
        expect: [{ severity: "error" }],
    },
]);
