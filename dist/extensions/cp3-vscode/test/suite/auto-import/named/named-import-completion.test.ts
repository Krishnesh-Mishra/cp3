// Auto-import / named imports
import { autoImportCases } from "../../harness";

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";
const utilWithTwo =
    "export function one(): number { return 1; }\nexport function two(): number { return 2; }\n";
const utilWithList =
    "function localHelper(): number { return 1; }\nconst LOCAL = \"s\";\nexport { localHelper, LOCAL };\n";
const utilWithRename =
    "function internalName(): number { return 1; }\nexport { internalName as publicName };\n";
const utilWithStar =
    "export * from \"./base.cp3\";\n";
const utilStarBase =
    "export function helper(): number { return 1; }\nexport const TOWER: string = \"up\";\n";

autoImportCases("Auto-import: named imports", [
    {
        name: "imported local function is offered in a fresh file",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "./util.cp3"',
    },
    {
        name: "offered named import inserts exactly one import line",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "two named exports from one module are both offered",
        source: "const a = __CURSOR__\n",
        modules: [["util.cp3", utilWithTwo]],
        expectOffered: "one",
        expectImportInserted: "import { one }",
    },
    {
        name: "second named export is offered as well",
        source: "const b = __CURSOR__\n",
        modules: [["util.cp3", utilWithTwo]],
        expectOffered: "two",
        expectImportInserted: "import { two }",
    },
    {
        name: "exported function declared below the export list is offered",
        source: "const helper = __CURSOR__\n",
        modules: [["util.cp3", utilWithList]],
        expectOffered: "localHelper",
        expectImportInserted: "import { localHelper }",
    },
    {
        name: "exported const from an export list is offered",
        source: "const shared = __CURSOR__\n",
        modules: [["util.cp3", utilWithList]],
        expectOffered: "LOCAL",
        expectImportInserted: "import { LOCAL }",
    },
    {
        name: "export with alias is offered under the alias",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", utilWithRename]],
        expectOffered: "publicName",
        expectImportInserted: "import { publicName }",
    },
    {
        name: "alias is not offered under the internal name",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", utilWithRename]],
        expectOffered: "internalName",
        expectNoItem: true,
    },
    {
        name: "export star re-exports a function under its own name",
        source: "const value = __CURSOR__\n",
        modules: [["barrel.cp3", utilWithStar], ["base.cp3", utilStarBase]],
        expectOffered: "helper",
        expectImportInserted: 'from "./barrel.cp3"',
    },
    {
        name: "export star re-exports a const under its own name",
        source: "const value = __CURSOR__\n",
        modules: [["barrel.cp3", utilWithStar], ["base.cp3", utilStarBase]],
        expectOffered: "TOWER",
        expectImportInserted: 'from "./barrel.cp3"',
    },
    {
        name: "unsaved open file's export is offered",
        source: "const joined = __CURSOR__\n",
        modules: [["fresh.cp3", "export function brandNew(): string { return \"new\"; }\n"]],
        expectOffered: "brandNew",
        expectImportInserted: "import { brandNew }",
    },
    {
        name: "combining named imports with an existing import keeps offering new ones",
        source: "import { one } from \"./util.cp3\";\nconst value = __CURSOR__\n",
        modules: [["util.cp3", utilWithTwo]],
        expectOffered: "two",
        expectImportInserted: "import { two }",
    },
    {
        name: "named import is offered after existing imports of other modules",
        source: "import { total } from \"./other.cp3\";\nconst value = __CURSOR__\n",
        modules: [["util.cp3", util], ["other.cp3", "export function total(): number { return 0; }\n"]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "imported name that already has an import in the file is not offered as a new import",
        source: "import { combine } from \"./util.cp3\";\nconst value = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "binding used but not exported stays unoffered",
        source: "const value = __CURSOR__\n",
        modules: [["util.cp3", "const hidden = 1;\nexport default function show(): number { return hidden; }\n"]],
        expectOffered: "hidden",
        expectNoItem: true,
    },
]);
