// Autocomplete / import completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: import completion", [
    {
        name: "imported std namespace member is offered after the dot",
        source: "import Math from \"std/math\";\nMath.__CURSOR__\n",
        expectPresent: ["abs", "floor", "ceil"],
    },
    {
        name: "std math members carry the documented name",
        source: "import Math from \"std/math\";\nMath.__CURSOR__\n",
        expectPresent: ["abs"],
    },
    {
        name: "std path member is offered after the dot",
        source: "import path from \"std/path\";\npath.__CURSOR__\n",
        expectPresent: ["join"],
    },
    {
        name: "console member is offered after the dot",
        source: "import console from \"std/console\";\nconsole.__CURSOR__\n",
        expectPresent: ["log"],
    },
    {
        name: "imported local function is offered in expression position",
        source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__;\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectPresent: ["combine"],
    },
    {
        name: "imported local class is offered in value position",
        source: "import { Widget } from \"./util.cp3\";\nconst w = new __CURSOR__();\n",
        modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]],
        expectPresent: ["Widget"],
    },
    {
        name: "named default import is offered",
        source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__;\n",
        modules: [["util.cp3", "export default function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectPresent: ["combine"],
    },
    {
        name: "export star re-export is offered in the importing file",
        source: "import { helper } from \"./barrel.cp3\";\nconst value = __CURSOR__;\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function helper(): number { return 1; }\n"]],
        expectPresent: ["helper"],
    },
    {
        name: "exported const from a re-exported module is offered",
        source: "import { KIND } from \"./barrel.cp3\";\nconst value = __CURSOR__;\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export const KIND: string = \"kind\";\n"]],
        expectPresent: ["KIND"],
    },
    {
        name: "two exported functions from one module are both offered",
        source: "import { one, two } from \"./util.cp3\";\nconst value = __CURSOR__;\n",
        modules: [["util.cp3", "export function one(): number { return 1; }\nexport function two(): number { return 2; }\n"]],
        expectPresent: ["one", "two"],
    },
    {
        name: "imported name used inside a call expression position",
        source: "import { combine } from \"./util.cp3\";\nconst result = combine(__CURSOR__);\n",
        modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]],
        expectPresent: ["combine"],
    },
    {
        name: "namespace member with a prefix is offered filtered",
        source: "import path from \"std/path\";\npath.jo__CURSOR__\n",
        expectPresent: ["join"],
    },
    {
        name: "std math abs is offered with prefix ma",
        source: "import Math from \"std/math\";\nMath.ab__CURSOR__\n",
        expectPresent: ["abs"],
    },
    {
        name: "imported interface offers its name in type position",
        source: "import { Config } from \"./util.cp3\";\nconst c: __CURSOR__\n",
        modules: [["util.cp3", "export interface Config {\n  retries: number;\n}\n"]],
        expectPresent: ["Config"],
    },
    {
        name: "imported enum is offered in value position",
        source: "import { Mode } from \"./util.cp3\";\nconst m = __CURSOR__;\n",
        modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]],
        expectPresent: ["Mode"],
    },
]);
