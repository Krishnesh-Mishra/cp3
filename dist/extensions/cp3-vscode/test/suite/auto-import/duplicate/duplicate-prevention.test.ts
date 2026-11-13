// Auto-import / duplicate import prevention
import { autoImportCases } from "../../harness";

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";

autoImportCases("Auto-import: duplicate import prevention", [
    {
        name: "an already-imported name is not offered a second import",
        source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "an existing import of the same name from elsewhere is not duplicated",
        source: "import { combine } from \"./other.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util], ["other.cp3", util]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "a name imported under an alias is still recognized as already bound",
        source: "import { combine as combineWith } from \"./util.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "a default import does not suppress a different named export",
        source: "import fallback from \"./util.cp3\";\nimport { combine } from \"./other.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", "export default function fallback(): string { return \"f\"; }\n"], ["other.cp3", util]],
        expectOffered: "combine",
        expectNoItem: true,
    },
    {
        name: "duplicate export of the same name from the same file is not offered twice",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
]);
