// Find references / barrels, re-exports and default exports
import { referencesCases } from "../../harness";

referencesCases("Find references: barrels and re-exports", [
    {
        name: "a symbol re-exported with an alias keeps its original declaration reference",
        source: "import { publicName } from \"./barrel.cp3\";\nconst value = __CURSOR__publicName();\n",
        modules: [["barrel.cp3", "export { internal as publicName } from \"./base.cp3\";\n"], ["base.cp3", "export function internal(): number { return 1; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "a default re-exported symbol is referenced at the import site",
        source: "import entry from \"./barrel.cp3\";\nconst value = __CURSOR__entry();\n",
        modules: [["barrel.cp3", "export { default } from \"./base.cp3\";\n"], ["base.cp3", "export default function entry(): number { return 1; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an export-star base module symbol is found at the barrel import site",
        source: "import { widget } from \"./barrel.cp3\";\nconst value = __CURSOR__widget();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function widget(): number { return 1; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "a namespace-style default import used twice is found twice",
        source: "import api from \"./lib.cp3\";\nconst first = __CURSOR__api.one();\nconst second = api.two();\n",
        modules: [["lib.cp3", "export default {\n  one(): number { return 1; },\n  two(): number { return 2; },\n};\n"]],
        expectSomeLines: [0],
        includeDeclaration: true,
    },
    {
        name: "a named export used under its export-list alias is found via the alias",
        source: "import { exposed } from \"./barrel.cp3\";\nconst value = __CURSOR__exposed();\n",
        modules: [["barrel.cp3", "const hidden = 1;\nexport { hidden as exposed };\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an exported class referenced across the barrel is found in the barrel consumer",
        source: "import { Widget } from \"./barrel.cp3\";\nconst w = new __CURSOR__Widget();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export class Widget {\n  render(): void {}\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an importing file with two symbols from one barrel finds each separately",
        source: "import { alpha, beta } from \"./barrel.cp3\";\nconst a = __CURSOR__alpha();\nconst b = beta();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function alpha(): number { return 1; }\nexport function beta(): number { return 2; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "a default export from a barrel is referenced at its single use site",
        source: "import impl from \"./barrel.cp3\";\nconst value = __CURSOR__impl();\n",
        modules: [["barrel.cp3", "export { default } from \"./base.cp3\";\n"], ["base.cp3", "export default function impl(): number { return 1; }\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an interface re-exported through a barrel is found at the use site",
        source: "import { Sized } from \"./barrel.cp3\";\nfunction f(s: __CURSOR__Sized): void {}\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export interface Sized {\n  size: number;\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "a const re-exported and used stays referenced at the barrel consumer",
        source: "import { MODE } from \"./barrel.cp3\";\nconst current = __CURSOR__MODE;\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export const MODE: string = \"fast\";\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
]);
