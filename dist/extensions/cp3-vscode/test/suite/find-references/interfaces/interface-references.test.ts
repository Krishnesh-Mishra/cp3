// Find references / interfaces and type-level symbols
import { referencesCases } from "../../harness";

referencesCases("Find references: interfaces", [
    {
        name: "an interface referenced in an implements clause is found",
        source: "interface Sized {\n  size: number;\n}\nclass Box implements __CURSOR__Sized {\n  size: number = 1;\n}\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 3],
    },
    {
        name: "an interface referenced in a type annotation is found",
        source: "interface Sized {\n  size: number;\n}\nfunction f(s: __CURSOR__Sized): number {\n  return s.size;\n}\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 3],
    },
    {
        name: "an interface referenced in two implements clauses is found at both",
        source: "interface Twin {\n  run(): void;\n}\nclass A implements Twin {\n  run(): void {}\n}\nclass B implements __CURSOR__Twin {\n  run(): void {}\n}\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [0, 3, 6],
    },
    {
        name: "a type alias referenced twice in annotations is found at both sites",
        source: "type Radians = number;\nfunction a(r: __CURSOR__Radians): void {}\nfunction b(r: Radians): void {}\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [0, 1, 2],
    },
    {
        name: "an enum referenced in value position is found",
        source: "enum Mode { Fast, Slow }\nconst m = __CURSOR__Mode.Fast;\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 1],
    },
    {
        name: "an enum used in two annotations is found at both sites",
        source: "enum Mode { Fast, Slow }\nfunction a(m: Mode): void {}\nfunction b(m: __CURSOR__Mode): void {}\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [0, 1, 2],
    },
    {
        name: "an interface member referenced across method and property is found",
        source: "interface Sized {\n  size: number;\n}\nfunction inspect(s: Sized): number {\n  return s.__CURSOR__size;\n}\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [1, 3],
    },
    {
        name: "an interface referenced across two files is found in both",
        source: "import { Widget } from \"./util.cp3\";\nconst a: __CURSOR__Widget = { title: \"a\" };\nconst b: Widget = { title: \"b\" };\n",
        modules: [["util.cp3", "export interface Widget {\n  title: string;\n}\n"]],
        expectSomeLines: [0, 1],
        includeDeclaration: true,
    },
    {
        name: "an interface referenced in a return annotation is found",
        source: "interface Payload {\n  data: string;\n}\nfunction make(): __CURSOR__Payload {\n  return { data: \"x\" };\n}\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 3],
    },
    {
        name: "a type alias used in a var declaration is found at the declaration site",
        source: "type Label = string;\nconst name: __CURSOR__Label = \"x\";\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 1],
    },
]);
