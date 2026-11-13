// Diagnostics / diagnostic positions and ranges
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: positions and ranges", [
    {
        name: "unknown member error points at the offending member line",
        source: "import Math from \"std/math\";\nlet x = Math.logh(2);\n",
        expect: [{ severity: "error", code: "CP3305", line: 2 }],
    },
    {
        name: "unknown member error for console input points at line 2",
        source: "import console from \"std/console\";\nconsole.inpu(\"Name? \");\n",
        expect: [{ severity: "error", code: "CP3305", line: 2 }],
    },
    {
        name: "unknown member error for console logh points at line 2",
        source: "import console from \"std/console\";\nconsole.logh(\"hi\");\n",
        expect: [{ severity: "error", code: "CP3305", line: 2 }],
    },
    {
        name: "unknown member error for JSON stringfy points at line 2",
        source: "import JSON from \"std/json\";\nlet text = JSON.stringfy(42);\n",
        expect: [{ severity: "error", code: "CP3305", line: 2 }],
    },
    {
        name: "writing to a missing member reports on line 3",
        source: "import console from \"std/console\";\nlet o = { a: 1 };\no.b = 2;\nconsole.log(`${o.a}`);\n",
        expect: [{ severity: "error", code: "CP3305", line: 3 }],
    },
    {
        name: "cannot find name reports the use line",
        source: "import console from \"std/console\";\nlet message = \"hi\";\nconsole.log(massage);\n",
        expect: [{ severity: "error", code: "CP3301", line: 3 }],
    },
    {
        name: "throwing a number reports on the try line",
        source: "import console from \"std/console\";\ntry { throw 42; } catch (e) { console.log(\"caught\"); }\n",
        expect: [{ severity: "error", code: "CP3509", line: 2 }],
    },
    {
        name: "a bare Math without import points at line 1",
        source: "let x = Math.random();\n",
        expect: [{ severity: "error", code: "CP3301", line: 1 }],
    },
    {
        name: "type mismatch reports on line 2",
        source: "let x: number = 1;\nx = \"bad\";\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "an error deeper in the file points at its own line not line 1",
        source: "const good = 1;\nconst alsoGood = 2;\nconst third = 3;\nlet x: number = 1;\nx = \"bad\";\n",
        expect: [{ severity: "error", line: 5 }],
    },
]);
