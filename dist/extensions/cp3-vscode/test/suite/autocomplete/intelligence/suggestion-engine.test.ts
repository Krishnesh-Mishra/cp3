// Autocomplete / the codebase-aware suggestion engine
import { completionCases } from "../../harness";

completionCases("Autocomplete: codebase-aware suggestion engine", [
    {
        name: "std/math offers only the documented closed member surface",
        source: "import Math from \"std/math\";\nconst x = Math.__CURSOR__",
        expectPresent: ["sin", "cos", "tan", "atan2", "hypot", "pow"],
        expectAbsent: ["secant", "sinish"],
    },
    {
        name: "std/math member query narrows by prefix",
        source: "import Math from \"std/math\";\nconst x = Math.si__CURSOR__",
        expectPresent: ["sin"],
        expectAbsent: ["cos", "tan"],
    },
    {
        name: "member completion works with no import at all (auto-import hint)",
        source: "const x = Math.__CURSOR__",
        expectPresent: ["sin", "cos"],
    },
    {
        name: "an aliased import resolves member completion",
        source: "import m from \"std/math\";\nconst x = m.__CURSOR__",
        expectPresent: ["sin", "cos", "tan"],
    },
    {
        name: "typing cons offers console.log with an auto-import edit",
        source: "const x = cons__CURSOR__",
        expectPresent: ["console.log()"],
    },
    {
        name: "typing sin offers the member-call snippet with an auto-import edit",
        source: "const x = sin__CURSOR__",
        expectPresent: ["sin()"],
    },
    {
        name: "a named stdlib export is offered with prefix",
        source: "const x = U__CURSOR__",
        expectPresent: ["URL"],
    },
    {
        name: "console binding offers the closed console surface",
        source: "import console from \"std/console\";\nconst x = console.__CURSOR__",
        expectPresent: ["log", "error", "warn", "input"],
        expectAbsent: ["secant"],
    },
    {
        name: "a workspace export from another file is offered at identifier position",
        source: "const x = hel__CURSOR__",
        modules: [["util.cp3", "export function helper(): number { return 1; }\n"]],
        expectPresent: ["helper"],
    },
    {
        name: "an unexported declaration is not offered from another file",
        source: "const x = secre__CURSOR__",
        modules: [["util.cp3", "const secretHelper = 41;\nexport function unrelated(): number { return 1; }\n"]],
        expectAbsent: ["secretHelper"],
    },
]);
