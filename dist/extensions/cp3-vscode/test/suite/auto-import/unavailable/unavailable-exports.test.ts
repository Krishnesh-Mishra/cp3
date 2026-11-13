// Auto-import / unused, unavailable or unreachable exports
import { autoImportCases } from "../../harness";

autoImportCases("Auto-import: unavailable exports", [
    {
        name: "an export only used inside its own module is not offered",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", "const internalHelper = 42;\nexport function external(): number { return internalHelper; }\n"]],
        expectOffered: "internalHelper",
        expectNoItem: true,
    },
    {
        name: "a module that failed to parse contributes nothing to the index",
        source: "const value = __CURSOR__\n",
        modules: [["broken.cp3", "export function combine(a: string\n"]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "an unexported const in an otherwise exporting module is not offered",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", "const SECRET = \"hidden\";\nexport function reveal(): string { return SECRET; }\n"]],
        expectOffered: "SECRET",
        expectNoItem: true,
    },
    {
        name: "an unresolvable std namespace member is not offered",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", "export function combine(a: string): string { return a; }\n"]],
        expectOffered: "nonexistent",
        expectNoItem: true,
    },
    {
        name: "re-exported name from a missing base module is not offered",
        source: "const value = __CURSOR__\n",
        modules: [["barrel.cp3", "export * from \"./missing.cp3\";\n"]],
        expectOffered: "helper",
        expectNoItem: true,
    },
]);
