// Code actions / quick fixes the server offers alongside diagnostics
import { codeActionCases } from "../../harness";

// Each fixture below produces (or deliberately avoids) a diagnostic; the
// expectation is about the quick fix the compiler offers for it. Codes are
// grounded in the conformance suite (CP3305 "did you mean", CP3301).
codeActionCases("Code actions: quick fixes", [
    {
        name: "mistyped console method offers a fix naming the right member",
        source: "import console from \"std/console\";\nconsole.__CURSOR__logh(\"hi\");\n",
        expectTitle: ["log"],
    },
    {
        name: "mistyped console method fix mentions 'log' explicitly",
        source: "import console from \"std/console\";\nconsole.__CURSOR__inpu(\"Name? \");\n",
        expectTitle: ["input"],
    },
    {
        name: "unknown member elsewhere offers the did-you-mean fix",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__logh(2);\n",
        expectTitle: ["log"],
    },
    {
        name: "cannot find name offers a fix naming the declared variable",
        source: "import console from \"std/console\";\nlet message = \"hi\";\nconsole.log(__CURSOR__massage);\n",
        expectTitle: ["message"],
    },
    {
        name: "JSON stringify typo offers the stringify fix",
        source: "import JSON from \"std/json\";\nlet text = JSON.__CURSOR__stringfy(42);\n",
        expectTitle: ["stringify"],
    },
    {
        name: "an import for a missing name offers the missing-import fix",
        source: "__CURSOR__let x = Math.random();\n",
        expectTitle: ["std/math"],
    },
    {
        name: "a clean file offers no quick fixes",
        source: "import Math from \"std/math\";\nconst x = __CURSOR__Math.abs(-1);\n",
        expectNone: true,
    },
    {
        name: "a valid object literal offers no quick fix",
        source: "const o = { a: 1 };\nconst read = __CURSOR__o.a;\n",
        expectNone: true,
    },
    {
        name: "a corrected name offers no further fix",
        source: "import console from \"std/console\";\nconsole.log(__CURSOR__\"x\");\n",
        expectNone: true,
    },
    {
        name: "a wrong member in an object literal offers the nearest fix",
        source: "import console from \"std/console\";\nconst o = { apple: 1 };\nconst b = o.__CURSOR__apply;\n",
        expectTitle: ["apple"],
    },
    {
        name: "a wrong member in a different object offers its own fix",
        source: "const state = { retries: 3 };\nconst attempts = state.__CURSOR__retires;\n",
        expectTitle: ["retries"],
    },
    {
        name: "a typo in a function argument offers the parameter-name fix",
        source: "function render(item: string): string { return item; }\nconst out = render(__CURSOR__item);\n",
        expectNone: true,
    },
    {
        name: "a missing Math import offers the std/math import fix",
        source: "__CURSOR__Math.random();\n",
        expectTitle: ["std/math"],
    },
    {
        name: "a missing console import offers the std/console import fix",
        source: "__CURSOR__console.log(\"hi\");\n",
        expectTitle: ["std/console"],
    },
    {
        name: "an EventEmitter import offers the fix to use the builtin instead",
        source: "import { __CURSOR__EventEmitter } from \"std/events\";\nlet e = new EventEmitter();\n",
        expectTitle: ["built"],
    },
    {
        name: "a valid Math member offers no quick fix",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__random();\n",
        expectNone: true,
    },
    {
        name: "a typo of sqrt offers the sqrt fix",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__scrt(4);\n",
        expectTitle: ["sqrt"],
    },
    {
        name: "a typo of floor offers the floor fix",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__flor(4.5);\n",
        expectTitle: ["floor"],
    },
    {
        name: "a typo of ceil offers the ceil fix",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__ceel(2);\n",
        expectTitle: ["ceil"],
    },
    {
        name: "an unknown member of a class instance offers no fix it cannot compute",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__stat();\n",
        expectTitle: ["start"],
    },
    {
        name: "an unknown member of an interface-typed value offers a fix",
        source: "interface Sized { size(): number }\nfunction f(s: Sized): void {\n  s.__CURSOR__siz();\n}\n",
        expectTitle: ["size"],
    },
    {
        name: "an unused import offers a remove-import quick fix",
        source: "import Math from \"std/math\";\nimport __CURSOR__console from \"std/console\";\nconst x = Math.abs(-1);\n",
        expectTitle: ["remove"],
    },
]);
