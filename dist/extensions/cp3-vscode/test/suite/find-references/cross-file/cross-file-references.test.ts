// Find references / cross-file module-scope symbols
import { referencesCases } from "../../harness";

referencesCases("Find references: cross-file symbols", [
    {
        name: "an imported function referenced twice in the importing file is found twice",
        source: "import { helper } from \"./util.cp3\";\nconst a = __CURSOR__helper();\nconst b = helper();\n",
        modules: [["util.cp3", "export function helper(): number { return 1; }\n"]],
        expectSomeLines: [0, 1, 2],
        includeDeclaration: true,
    },
    {
        name: "a default export referenced once in the importing file is found",
        source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export default function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "a re-exported symbol referenced through a barrel is found",
        source: "import { helper } from \"./barrel.cp3\";\nconst out = __CURSOR__helper();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function helper(): number { return 1; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an alias referenced indirectly keeps the original declaration",
        source: "import { combine as joinFiles } from \"./util.cp3\";\nconst joined = __CURSOR__joinFiles(\"a\", \"b\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an imported const referenced in a template is found",
        source: "import { TITLE } from \"./util.cp3\";\nconst label = `${__CURSOR__TITLE}`;\n",
        modules: [["util.cp3", "export const TITLE: string = \"t\";\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an imported class referenced in two places is found in both",
        source: "import { Widget } from \"./util.cp3\";\nconst a = new __CURSOR__Widget();\nconst b = new Widget();\n",
        modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]],
        expectSomeLines: [0, 1, 2],
        includeDeclaration: true,
    },
    {
        name: "a local module's symbol referenced across three files is found",
        source: "import { combine } from \"./util.cp3\";\nimport other from \"./other.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"], ["other.cp3", "export default function other(): number { return 1; }\n"]],
        expectSomeLines: [0, 2],
        includeDeclaration: true,
    },
    {
        name: "an exported const in a sibling file is found at the import site",
        source: "import { LIMIT } from \"./constants.cp3\";\nconst value = __CURSOR__LIMIT;\n",
        modules: [["constants.cp3", "export const LIMIT: number = 10;\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an unexported implementation detail produces no cross-file references",
        source: "import helperUnavailable from \"./util.cp3\";\nconst value = __CURSOR__helperUnavailable();\n",
        modules: [["util.cp3", "const internal = 1;\nexport default function external(): number {\n  return internal;\n}\n"]],
        expectSomeLines: [0],
        includeDeclaration: true,
    },
    {
        name: "the import edge itself is reported as a reference for the symbol",
        source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
]);
