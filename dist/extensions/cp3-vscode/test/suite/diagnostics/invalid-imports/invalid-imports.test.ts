// Diagnostics / invalid imports
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: invalid imports", [
    {
        name: "EventEmitter must not be imported; it is a language builtin",
        source: "import { EventEmitter } from \"std/events\";\nlet e = new EventEmitter();\n",
        expect: [{ severity: "error", code: "CP3601", line: 1, contains: "language builtin" }],
    },
    {
        name: "importing console as a named binding is an error",
        source: "import { console } from \"std/console\";\nconsole.log(\"x\");\n",
        expect: [{ severity: "error", code: "CP3304", line: 1, contains: "use the default import" }],
    },
    {
        name: "importing JSON as a named binding is an error",
        source: "import { JSON } from \"std/json\";\n",
        expect: [{ severity: "error", code: "CP3304", contains: "use the default import" }],
    },
    {
        name: "importing Math as a named binding is an error",
        source: "import { Math } from \"std/math\";\n",
        expect: [{ severity: "error", code: "CP3304" }],
    },
    {
        name: "a nonexistent named import is an error naming the missing export",
        source: "import { nonexistent } from \"std/collections\";\n",
        expect: [{ severity: "error", code: "CP3xxxx", line: 1, contains: "nonexistent" }],
    },
    {
        name: "an import from a nonexistent module is an error",
        source: "import x from \"std/doesnotexist\";\n",
        expect: [{ severity: "error", code: "CP3xxxx", line: 1, contains: "doesnotexist" }],
    },
    {
        name: "importing Mutex from std/collections is an error",
        source: "import { Mutex } from \"std/collections\";\nlet m = new Mutex();\n",
        expect: [{ severity: "error", code: "CP3xxxx", line: 1, contains: "Mutex" }],
    },
    {
        name: "bare Math usage without import reports the missing import",
        source: "let x = Math.random();\n",
        expect: [{ severity: "error", code: "CP3301", line: 1, contains: "std/math" }],
    },
    {
        name: "bare console usage without import reports the missing import",
        source: "console.log(\"hello\");\n",
        expect: [{ severity: "error", code: "CP3301", line: 1 }],
    },
    {
        name: "importing the same name twice from one module is an error",
        source: "import { one, one } from \"./util.cp3\";\n",
        modules: [["util.cp3", "export function one(): number { return 1; }\n"]],
        expect: [{ severity: "error" }],
    },
    {
        name: "a valid std import is not an error",
        source: "import Math from \"std/math\";\nlet x = Math.random();\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "a valid named local import is not an error",
        source: "import { one } from \"./util.cp3\";\nimport console from \"std/console\";\nconsole.log(`${one()}`);\n",
        modules: [["util.cp3", "export function one(): number { return 1; }\n"]],
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "a valid default local import is not an error",
        source: "import util from \"./util.cp3\";\n",
        modules: [["util.cp3", "export default function util(): number { return 1; }\n"]],
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "unused import still parses without a hard error",
        source: "import Math from \"std/math\";\n",
        expect: [],
        expectNoErrors: true,
    },
    {
        name: "importing a type as a value parses without a hard error",
        source: "import { Config } from \"./util.cp3\";\nimport console from \"std/console\";\nconsole.log(\"done\");\n",
        modules: [["util.cp3", "export interface Config { retries: number; }\n"]],
        expect: [],
        expectNoErrors: true,
    },
]);
