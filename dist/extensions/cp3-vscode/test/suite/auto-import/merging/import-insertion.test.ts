// Auto-import / existing import merging and insertion point
import { autoImportCases } from "../../harness";

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";

autoImportCases("Auto-import: merging and insertion point", [
    {
        name: "accepting a suggestion inserts below the last existing import",
        source: "import { one } from \"./util-one.cp3\";\nimport console from \"std/console\";\nconst joined = __CURSOR__\n",
        modules: [["util-one.cp3", "export function one(): number { return 1; }\n"],
                  ["util-two.cp3", "export function combine(a: string): string { return a; }\n"]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "import is inserted above the first non-import statement",
        source: "const before = 1;\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "import block with a comment line still inserts an import",
        source: "import { one } from \"./util-one.cp3\";\n// keep me\nconst joined = __CURSOR__\n",
        modules: [["util-one.cp3", "export function one(): number { return 1; }\n"],
                  ["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "insert-before-first-statement handles a leading comment block",
        source: "// header comment\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "existing side-effect import is left untouched",
        source: "import \"./setup.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["setup.cp3", "console.log(\"setup\");\n"], ["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "a named import from the same module keeps its specifier shape",
        source: "import { one } from \"./util.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", "export function one(): number { return 1; }\nexport function combine(a: string): string { return a; }\n"]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "import is suggested in a file that already has other imports above the old code",
        source: "import console from \"std/console\";\nimport Math from \"std/math\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "a trailing statement before additional imports does not block suggestions",
        source: "const init = 1;\nimport console from \"std/console\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "no auto-import is suggested for an undeclared stdlib name already in use",
        source: "const value = __CURSOR__\n",
        modules: [["console.cp3", "export const log: string = \"local\";\n"]],
        expectOffered: "console",
        expectImportInserted: "console.cp3",
    },
    {
        name: "offered edit text always names the correct relative file",
        source: "const joined = __CURSOR__\n",
        modules: [["deep/path/combine.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "deep/path/combine.cp3",
    },
]);
