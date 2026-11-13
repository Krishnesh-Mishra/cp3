// Go to definition / types, enums and imports
import { definitionCases } from "../../harness";

definitionCases("Go to definition: types and enums", [
    {
        name: "an enum used in a type position jumps to the enum",
        source: "enum Direction { North, South }\nfunction travel(d: __CURSOR__Direction): void {}\n",
        expectLines: [0],
    },
    {
        name: "an enum used in a value position jumps to the enum",
        source: "enum Direction { North, South }\nconst head = __CURSOR__Direction.North;\n",
        expectLines: [0],
    },
    {
        name: "a type alias jumps to its declaration",
        source: "type Radians = number;\nfunction toDegrees(r: __CURSOR__Radians): number { return 0; }\n",
        expectLines: [0],
    },
    {
        name: "a type alias used by a function return jumps to the alias",
        source: "type Label = string;\nfunction make(): __CURSOR__Label { return \"x\"; }\n",
        expectLines: [0],
    },
    {
        name: "a type alias re-exported from a barrel jumps to its source",
        source: "import { Pixels } from \"./barrel.cp3\";\nconst w: __CURSOR__Pixels = 10;\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export type Pixels = number;\n"]],
        expectLines: [0],
    },
    {
        name: "an enum used in an annotation across files jumps into the module",
        source: "import { Mode } from \"./util.cp3\";\nfunction run(m: __CURSOR__Mode): void {}\n",
        modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]],
        expectLines: [0],
    },
    {
        name: "a generic interface used across files jumps into the module",
        source: "import { Pair } from \"./util.cp3\";\nfunction inspect(p: __CURSOR__Pair<number, string>): void {}\n",
        modules: [["util.cp3", "export interface Pair<A, B> {\n  first: A;\n  second: B;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a type alias used across files jumps into the defining module",
        source: "import { Radians } from \"./util.cp3\";\nfunction rotate(r: __CURSOR__Radians): number { return 0; }\n",
        modules: [["util.cp3", "export type Radians = number;\n"]],
        expectLines: [0],
    },
    {
        name: "a default-exported type alias jumps into the defining module",
        source: "import Settings from \"./util.cp3\";\nconst s: __CURSOR__Settings = { retries: 1 };\n",
        modules: [["util.cp3", "export default interface Settings {\n  retries: number;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a namespaced type member jumps to the member declaration",
        source: "interface Config {\n  retries: number;\n}\nconst c: Config = { retries: 1 };\nconst retries = c.__CURSOR__retries;\n",
        expectLines: [1],
    },
]);
