// Auto-import / directory index resolution
import { autoImportCases } from "../../harness";

const indexModule =
    "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";
const plainFile =
    "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";

autoImportCases("Auto-import: directory index resolution", [
    {
        name: "a package folder with an index module resolves to the folder specifier",
        source: "const joined = __CURSOR__\n",
        modules: [["packages/mathx/index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "mathx",
    },
    {
        name: "a folder with an index resolves without the index file name",
        source: "const joined = __CURSOR__\n",
        modules: [["packages/mathx/index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "mathx\";",
    },
    {
        name: "a sibling index module resolves to the folder specifier",
        source: "const joined = __CURSOR__\n",
        modules: [["mathx/index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "./mathx",
    },
    {
        name: "index file with re-exports is discovered as a candidate",
        source: "const joined = __CURSOR__\n",
        modules: [["mathx/index.cp3", "export * from \"./inner.cp3\";\n"], ["mathx/inner.cp3", plainFile]],
        expectOffered: "combine",
        expectImportInserted: "mathx",
    },
    {
        name: "nested package with an index resolves to the nested specifier",
        source: "const joined = __CURSOR__\n",
        modules: [["a/b/mathx/index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "a/b/mathx",
    },
    {
        name: "index-style import carries the same export the file declares",
        source: "const joined = __CURSOR__\n",
        modules: [["pkg/index.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectOffered: "combine",
        expectImportInserted: "pkg",
    },
    {
        name: "a folder with both index and a named module prefers the explicit file",
        source: "const joined = __CURSOR__\n",
        modules: [["mathx/index.cp3", "export const UNRELATED: number = 1;\n"], ["mathx/combine.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "combine.cp3",
    },
    {
        name: "a deep index (src/index) keeps the subpath shape",
        source: "const joined = __CURSOR__\n",
        modules: [["packages/mathx/src/index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "mathx",
    },
    {
        name: "root-level index module resolves to the relative dot slash",
        source: "const joined = __CURSOR__\n",
        modules: [["index.cp3", indexModule]],
        expectOffered: "combine",
        expectImportInserted: "./index.cp3",
    },
    {
        name: "an index does not shadow a same-name explicit module import",
        source: "const joined = __CURSOR__\n",
        modules: [["mathx.cp3", indexModule], ["mathx/index.cp3", "export const OTHER: number = 1;\n"]],
        expectOffered: "combine",
        expectImportInserted: "mathx.cp3",
    },
]);
