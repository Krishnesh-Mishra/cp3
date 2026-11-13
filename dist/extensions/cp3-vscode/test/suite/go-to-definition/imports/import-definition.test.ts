// Go to definition / imports and exports navigation
import { definitionCases } from "../../harness";

definitionCases("Go to definition: imports", [
    {
        name: "a local import jumps to the exported declaration",
        source: "import { alpha } from \"./lib.cp3\";\nconst value = __CURSOR__alpha;\n",
        modules: [["lib.cp3", "export function alpha(): number { return 1; }\n"]],
        expectLines: [0],
    },
    {
        name: "a local import in another folder jumps correctly across directories",
        source: "import { alpha } from \"./nested/lib.cp3\";\nconst value = __CURSOR__alpha;\n",
        modules: [["nested/lib.cp3", "export const alpha: string = \"a\";\n"]],
        expectLines: [0],
    },
    {
        name: "aliased import jumps to the original declaration",
        source: "import { combine as joinTogether } from \"./lib.cp3\";\nconst value = __CURSOR__joinTogether(\"a\", \"b\");\n",
        modules: [["lib.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "default import jumps to the default-declared export",
        source: "import one from \"./lib.cp3\";\nconst value = __CURSOR__one();\n",
        modules: [["lib.cp3", "export default function one(): number { return 1; }\n"]],
        expectLines: [0],
    },
    {
        name: "renamed default export jumps to its declaration",
        source: "import special from \"./lib.cp3\";\nconst value = __CURSOR__special();\n",
        modules: [["lib.cp3", "function internalName(): number { return 1; }\nexport default internalName;\n"]],
        expectLines: [0],
    },
    {
        name: "barrel re-export resolves to the underlying declaration",
        source: "import { widget } from \"./barrel.cp3\";\nconst value = __CURSOR__widget();\n",
        modules: [["barrel.cp3", "export * from \"./source.cp3\";\n"], ["source.cp3", "export function widget(): string { return \"w\"; }\n"]],
        expectLines: [0],
    },
    {
        name: "named re-export resolves to the underlying declaration",
        source: "import { renamedThing } from \"./barrel.cp3\";\nconst value = __CURSOR__renamedThing();\n",
        modules: [["barrel.cp3", "export { originalThing as renamedThing } from \"./source.cp3\";\n"], ["source.cp3", "export function originalThing(): string { return \"o\"; }\n"]],
        expectLines: [0],
    },
    {
        name: "an exported const from a multi-declaration module jumps correctly",
        source: "import { two } from \"./lib.cp3\";\nconst value = __CURSOR__two;\n",
        modules: [["lib.cp3", "export const one: number = 1;\nexport const two: number = 2;\nexport const three: number = 3;\n"]],
        expectLines: [1],
    },
    {
        name: "an import is not jumped to an unrelated std declaration",
        source: "import Math from \"std/math\";\nconst value = __CURSOR__Math.abs(-1);\n",
        expectNone: true,
    },
    {
        name: "a circularly imported module still resolves the declaration",
        source: "import { helper } from \"./cycle-a.cp3\";\nconst value = __CURSOR__helper();\n",
        modules: [["cycle-a.cp3", "import { b } from \"./cycle-b.cp3\";\nexport function helper(): number {\n  return b;\n}\n"], ["cycle-b.cp3", "import { helper } from \"./cycle-a.cp3\";\nexport const b: number = 1;\n"]],
        expectLines: [0],
    },
]);
