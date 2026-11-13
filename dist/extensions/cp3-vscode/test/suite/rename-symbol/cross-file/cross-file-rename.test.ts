// Rename symbol / cross-file renames
import { renameCases } from "../../harness";

renameCases("Rename symbol: cross-file renames", [
    {
        name: "an imported function is renamed at its declaration in the module",
        source: "import { helper } from \"./util.cp3\";\nconst value = __CURSOR__helper();\n",
        modules: [["util.cp3", "export function helper(): number { return 1; }\n"]],
        newName: "assist",
    },
    {
        name: "an imported function renamed in the importing file renames the module declaration too",
        source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\ncombine(\"x\", \"y\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        newName: "merge",
    },
    {
        name: "an imported class renamed in the importing file renames the module declaration",
        source: "import { Widget } from \"./util.cp3\";\nconst w = new __CURSOR__Widget();\n",
        modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]],
        newName: "Panel",
    },
    {
        name: "an interface imported from another file is renamed in both files",
        source: "import { Sized } from \"./util.cp3\";\nfunction f(s: __CURSOR__Sized): number {\n  return s.size;\n}\n",
        modules: [["util.cp3", "export interface Sized {\n  size: number;\n}\n"]],
        newName: "Measurable",
    },
    {
        name: "a default-exported function renamed in the importing file renames both",
        source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export default function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        newName: "merge",
    },
    {
        name: "an exported const renamed in the importing file renames the module",
        source: "import { LIMIT } from \"./util.cp3\";\nconst value = __CURSOR__LIMIT;\nconst again = LIMIT * 2;\n",
        modules: [["util.cp3", "export const LIMIT: number = 10;\n"]],
        newName: "CAP",
    },
    {
        name: "an alias import renamed renames only the local alias binding",
        source: "import { combine as joinItems } from \"./util.cp3\";\nconst joined = __CURSOR__joinItems(\"a\", \"b\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        newName: "gather",
    },
    {
        name: "a symbol re-exported with an alias renamed at the import site renames one place",
        source: "import { exposed } from \"./barrel.cp3\";\nconst joined = __CURSOR__exposed();\n",
        modules: [["barrel.cp3", "export { hidden as exposed } from \"./base.cp3\";\n"], ["base.cp3", "export function hidden(): number { return 1; }\n"]],
        newName: "revealed",
    },
    {
        name: "a function used in three files is renamed in all of them",
        source: "import { combine } from \"./util.cp3\";\nimport other from \"./other.cp3\";\nconst a = __CURSOR__combine(\"x\", \"y\");\ncombine(\"p\", \"q\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"],
                  ["other.cp3", "import { combine } from \"./util.cp3\";\nexport default function other(): number {\n  return combine(\"1\", \"2\").length;\n}\n"]],
        newName: "merge",
    },
    {
        name: "a shared enum member renamed in one file renames everywhere it is used",
        source: "import { Mode } from \"./util.cp3\";\nconst m = __CURSOR__Mode.Fast;\n",
        modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]],
        newName: "Quick",
    },
]);
