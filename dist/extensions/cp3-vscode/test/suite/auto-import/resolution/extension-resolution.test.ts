// Auto-import / extension resolution
import { autoImportCases } from "../../harness";

const impl = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";

autoImportCases("Auto-import: extension resolution", [
    {
        name: "the inserted import keeps the .cp3 extension the compiler expects",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", impl]],
        expectOffered: "combine",
        expectImportInserted: ".cp3\"",
    },
    {
        name: "no double extension is produced",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", impl]],
        expectOffered: "combine",
        expectImportInserted: "util.cp3",
    },
    {
        name: "a sibling module with dashes resolves with exactly one extension",
        source: "const joined = __CURSOR__\n",
        modules: [["lib/my-util.cp3", impl]],
        expectOffered: "combine",
        expectImportInserted: "my-util.cp3",
    },
    {
        name: "index resolution still keeps the index file explicit or the folder only",
        source: "const joined = __CURSOR__\n",
        modules: [["mathx/index.cp3", impl]],
        expectOffered: "combine",
        expectImportInserted: "mathx",
    },
    {
        name: "a deep relative path to an implementation file keeps the extension",
        source: "const joined = __CURSOR__\n",
        modules: [["src/deep/nested/util.cp3", impl]],
        expectOffered: "combine",
        expectImportInserted: "util.cp3",
    },
]);
