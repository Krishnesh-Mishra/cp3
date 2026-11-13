// Auto-import / default imports
import { autoImportCases } from "../../harness";

const defaultCombiner =
    "export default function combineWith(a: string, b: string): string {\n  return a + b;\n}\n";
const defaultClass =
    "export default class Widget {\n  render(): void {}\n}\n";
const defaultConstArrow =
    "const handler = (value: number): number => value + 1;\nexport default handler;\n";
const defaultObject =
    "export default { retries: 3, timeout: 100 };\n";

autoImportCases("Auto-import: default imports", [
    {
        name: "default-exported function is offered",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", defaultCombiner]],
        expectOffered: "combineWith",
        expectImportInserted: "import",
    },
    {
        name: "default-exported class is offered in value position",
        source: "const widget = __CURSOR__\n",
        modules: [["util.cp3", defaultClass]],
        expectOffered: "Widget",
        expectImportInserted: "import",
    },
    {
        name: "default-exported const arrow is offered under its own name",
        source: "const next = __CURSOR__\n",
        modules: [["util.cp3", defaultConstArrow]],
        expectOffered: "handler",
        expectImportInserted: "import",
    },
    {
        name: "default object export is not offered as a symbol",
        source: "const cfg = __CURSOR__\n",
        modules: [["util.cp3", defaultObject]],
        expectOffered: "retries",
        expectNoItem: true,
    },
    {
        name: "default export with a different name is offered under its declared name",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", "function internalThing(a: string): string { return a; }\nexport default internalThing;\n"]],
        expectOffered: "internalThing",
        expectImportInserted: "import",
    },
    {
        name: "a module with default and named exports offers the named one",
        source: "const joined = __CURSOR__\n",
        modules: [["util.cp3", "export default function fallback(): string { return \"f\"; }\nexport function combine(a: string): string { return a; }\n"]],
        expectOffered: "combine",
        expectImportInserted: "import { combine }",
    },
    {
        name: "a default import specialization is not duplicated when already imported",
        source: "import combineWith from \"./util.cp3\";\nconst joined = __CURSOR__\n",
        modules: [["util.cp3", defaultCombiner]],
        expectOffered: "combineWith",
        expectNoItem: true,
    },
    {
        name: "default export of an arrow function is offered in a call",
        source: "const ready = __CURSOR__\n",
        modules: [["util.cp3", "export default (): number => 42;\n"]],
        expectOffered: "default",
        expectNoItem: true,
    },
    {
        name: "default export of an interface is offered in type position",
        source: "const c: __CURSOR__\n",
        modules: [["util.cp3", "export default interface Config { retries: number; }\n"]],
        expectOffered: "Config",
        expectImportInserted: "import",
    },
    {
        name: "default export of a class inserts an import naming the class",
        source: "const widget = new __CURSOR__();\n",
        modules: [["util.cp3", defaultClass]],
        expectOffered: "Widget",
        expectImportInserted: "Widget",
    },
]);
