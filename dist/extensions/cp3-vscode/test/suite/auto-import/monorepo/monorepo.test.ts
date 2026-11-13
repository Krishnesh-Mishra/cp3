// Auto-import / monorepo and multi-package workspaces
import { autoImportCases } from "../../harness";

const libA = "export function fromLibA(a: string): string {\n  return a;\n}\n";
const appB = "export function fromAppB(a: string): string {\n  return a;\n}\n";

autoImportCases("Auto-import: monorepo resolution", [
    {
        name: "an export from a sibling package's index is offered",
        source: "const value = __CURSOR__\n",
        modules: [["packages/lib-a/index.cp3", libA]],
        expectOffered: "fromLibA",
        expectImportInserted: "lib-a",
    },
    {
        name: "an export from one package is offered to another package's source",
        source: "const value = __CURSOR__\n",
        modules: [["packages/app/entry.cp3", "export function entry(): number { return 1; }\n"],
                  ["packages/lib-a/index.cp3", libA]],
        expectOffered: "fromLibA",
        expectImportInserted: "lib-a",
    },
    {
        name: "an export from the app package itself is offered",
        source: "const value = __CURSOR__\n",
        modules: [["packages/app/util.cp3", appB]],
        expectOffered: "fromAppB",
        expectImportInserted: "app",
    },
    {
        name: "two packages exporting the same name both stay candidates",
        source: "const value = __CURSOR__\n",
        modules: [["packages/one/shared.cp3", "export function packageShared(a: string): string { return a; }\n"],
                  ["packages/two/shared.cp3", "export function packageShared(b: string): string { return b; }\n"]],
        expectOffered: "packageShared",
    },
    {
        name: "a nested workspace package resolves relative to the importing file",
        source: "const value = __CURSOR__\n",
        modules: [["packages/group/nested/index.cp3", libA]],
        expectOffered: "fromLibA",
        expectImportInserted: "nested",
    },
]);
