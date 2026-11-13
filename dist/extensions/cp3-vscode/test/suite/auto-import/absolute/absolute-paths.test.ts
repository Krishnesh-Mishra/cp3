// Auto-import / absolute paths and aliases
import { autoImportCases } from "../../harness";

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";

autoImportCases("Auto-import: absolute paths and aliases", [
    {
        name: "a vendored package folder inside the workspace resolves relatively",
        source: "const joined = __CURSOR__\n",
        modules: [["vendor/pkg/util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "vendor/pkg/util.cp3",
    },
    {
        name: "absolute specifier of a local package root import inserts a package specifier",
        source: "const joined = __CURSOR__\n",
        modules: [["pkg/util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "pkg/util.cp3",
    },
    {
        name: "a local module sharing its name with a std module still imports relatively",
        source: "const joined = __CURSOR__\n",
        modules: [["math.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: 'from "./math.cp3"',
    },
    {
        name: "aliased package specifier offer would still be a valid tsconfig-style path",
        source: "const joined = __CURSOR__\n",
        modules: [["src/shared/combine.ts".replace(".ts", ".cp3"), util]],
        expectOffered: "combine",
        expectImportInserted: "src/shared/combine.cp3",
    },
    {
        name: "a root-level module imports with the dot slash prefix",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "./util.cp3",
    },
    {
        name: "index at the package root resolves to the root specifier",
        source: "const joined = __CURSOR__\n",
        modules: [["pkg/index.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "pkg",
    },
    {
        name: "deep package subpath keeps the subpath shape",
        source: "const joined = __CURSOR__\n",
        modules: [["pkg/lib/deep/util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "pkg/lib/deep/util.cp3",
    },
    {
        name: "a trailing-slash alias is not offered for a file module",
        source: "const joined = __CURSOR__\n",
        modules: [["lib/util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "lib/util.cp3",
    },
    {
        name: "a bare specifier from the importing file is never rewritten to an absolute OS path",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "from \"./",
    },
    {
        name: "typed package import keeps the package name segment first",
        source: "const joined = __CURSOR__\n",
        modules: [["packages/mathx/src/index.cp3", util]],
        expectOffered: "combine",
        expectImportInserted: "mathx",
    },
]);
