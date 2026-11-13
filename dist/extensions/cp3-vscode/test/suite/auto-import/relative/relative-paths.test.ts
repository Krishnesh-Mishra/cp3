// Auto-import / relative paths
import { autoImportCases } from "../../harness";

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";
const helper = "export function single(): number { return 1; }\n";

autoImportCases("Auto-import: relative paths", [
    {
        name: "sibling module is imported with ./ prefix",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "./util.cp3"',
    },
    {
        name: "module one directory up is imported with ../ prefix",
        source: "const joined = __CURSOR__\n",
        mainPath: "nested/main.cp3",
        modules: [["shared.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "../shared.cp3"',
    },
    {
        name: "deeply nested sibling import keeps the relative shape",
        source: "const value = __CURSOR__\n",
        modules: [["lib/math/trig.cp3", helper]],
        expectOffered: "single",
        expectImportInserted: 'from "./lib/math/trig.cp3"',
    },
    {
        name: "import inserts with a forward-slash path on every platform",
        source: "const value = __CURSOR__\n",
        modules: [["lib/deep/nested/thing.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "lib/deep/nested/thing.cp3",
    },
    {
        name: "relative import from a nested folder into a sibling folder",
        source: "const value = __CURSOR__\n",
        modules: [["shared/util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "./shared/util.cp3"',
    },
    {
        name: "same-module name completion still points at the right file",
        source: "const value = __CURSOR__\n",
        modules: [["lib/a/inner.cp3", util], ["lib/b/unrelated.cp3", helper]],
        expectOffered: "combine",
        expectImportInserted: "a/inner.cp3",
    },
    {
        name: "relative path to a file with dashes is inserted verbatim",
        source: "const value = __CURSOR__\n",
        modules: [["lib/my-helper.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "my-helper.cp3",
    },
    {
        name: "relative path to a file with underscores is inserted verbatim",
        source: "const value = __CURSOR__\n",
        modules: [["lib/my_helper.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "my_helper.cp3",
    },
    {
        name: "relative path to a file with dots keeps them in the specifier",
        source: "const value = __CURSOR__\n",
        modules: [["lib/v1.util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "v1.util.cp3",
    },
    {
        name: "a nearer module wins the offered import over a farther one",
        source: "const value = __CURSOR__\n",
        modules: [["near.cp3", util], ["far/deep/near.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "./near.cp3"',
    },
]);
