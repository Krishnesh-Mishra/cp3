// Integration / cross-file type resolution
import { completionCases, definitionCases, hoverCases } from "../../harness";

completionCases("Integration: cross-file completion", [
    { name: "a cross-file class instance offers its members", source: "import { Widget } from \"./util.cp3\";\nconst w = new Widget();\nw.__CURSOR__\n", modules: [["util.cp3", "export class Widget {\n  render(): void {}\n  label: string = \"x\";\n}\n"]], expectPresent: ["render", "label"] },
    { name: "a cross-file class static member is offered on the class", source: "import { Counter } from \"./util.cp3\";\nCounter.__CURSOR__\n", modules: [["util.cp3", "export class Counter {\n  static total: number = 0;\n}\n"]], expectPresent: ["total"] },
    { name: "a cross-file interface-typed parameter offers its members", source: "import { Config } from \"./util.cp3\";\nfunction f(c: Config): void {\n  c.__CURSOR__\n}\n", modules: [["util.cp3", "export interface Config {\n  retries: number;\n}\n"]], expectPresent: ["retries"] },
    { name: "a cross-file enum is offered in a type position", source: "import { Mode } from \"./util.cp3\";\nfunction run(m: __CURSOR__Mode): void {}\n", modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]], expectPresent: ["Mode"] },
    { name: "a cross-file generic class narrows members instantiated with string", source: "import { Box } from \"./util.cp3\";\nconst b = new Box<string>();\nb.value.__CURSOR__\n", modules: [["util.cp3", "export class Box<T> {\n  value: T = 0 as T;\n}\n"]], expectPresent: ["toUpperCase"] },
    { name: "a cross-file generic class does not leak other instantiations members", source: "import { Box } from \"./util.cp3\";\nconst b = new Box<number>();\nb.value.__CURSOR__\n", modules: [["util.cp3", "export class Box<T> {\n  value: T = 0 as T;\n}\n"]], expectAbsent: ["toUpperCase"] },
    { name: "a cross-file function is offered in expression position", source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__\n", modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]], expectPresent: ["combine"] },
    { name: "a cross-file exported const is offered in expression position", source: "import { LIMIT } from \"./util.cp3\";\nconst value = __CURSOR__\n", modules: [["util.cp3", "export const LIMIT: number = 10;\n"]], expectPresent: ["LIMIT"] },
    { name: "a cross-file default-exported function is offered", source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__\n", modules: [["util.cp3", "export default function combine(a: string): string {\n  return a;\n}\n"]], expectPresent: ["combine"] },
    { name: "a cross-file exported interface is offered in type position", source: "import { Sized } from \"./util.cp3\";\nconst ref: __CURSOR__Sized = { size: () => 1 };\n", modules: [["util.cp3", "export interface Sized {\n  size(): number;\n}\n"]], expectPresent: ["Sized"] },
]);

hoverCases("Integration: cross-file hover", [
    { name: "a cross-file class hover reports the class name", source: "import { Widget } from \"./util.cp3\";\nconst w = new __CURSOR__Widget();\n", modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]], contains: ["Widget"] },
    { name: "a cross-file function hover reports its signature", source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n", modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]], contains: ["combine"] },
    { name: "a cross-file interface hover reports the interface", source: "import { Sized } from \"./util.cp3\";\nfunction f(s: __CURSOR__Sized): void {}\n", modules: [["util.cp3", "export interface Sized {\n  size(): number;\n}\n"]], contains: ["Sized"] },
    { name: "a cross-file generic class hover reports the class", source: "import { Box } from \"./util.cp3\";\nconst b = new __CURSOR__Box<string>();\n", modules: [["util.cp3", "export class Box<T> {\n  value: T = 0 as T;\n}\n"]], contains: ["Box"] },
    { name: "a cross-file enum hover reports the enum", source: "import { Mode } from \"./util.cp3\";\nconst m = __CURSOR__Mode.Fast;\n", modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]], contains: ["Mode"] },
    { name: "a cross-file function parameter hover reports the type", source: "import { combine } from \"./util.cp3\";\nconst joined = combine(__CURSOR__\"a\", \"b\");\n", modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]], contains: ["string"] },
    { name: "a cross-file const hover reports the type", source: "import { LIMIT } from \"./util.cp3\";\nconst read = __CURSOR__LIMIT;\n", modules: [["util.cp3", "export const LIMIT: number = 10;\n"]], contains: ["number"] },
    { name: "a cross-file interface method hover reports the member", source: "import { Sized } from \"./util.cp3\";\nfunction f(s: Sized): void {\n  s.__CURSOR__size();\n}\n", modules: [["util.cp3", "export interface Sized {\n  size(): number;\n}\n"]], contains: ["size"] },
    { name: "a cross-file class method hover reports the method", source: "import { Widget } from \"./util.cp3\";\nconst w = new Widget();\nw.__CURSOR__render();\n", modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]], contains: ["render"] },
    { name: "a cross-file default export hover reports the function", source: "import combine from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\");\n", modules: [["util.cp3", "export default function combine(a: string): string {\n  return a;\n}\n"]], contains: ["combine"] },
]);

definitionCases("Integration: cross-file definition", [
    { name: "a cross-file function call jumps to the module declaration", source: "import { combine } from \"./util.cp3\";\nconst joined = __CURSOR__combine(\"a\", \"b\");\n", modules: [["util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"]], expectLines: [0] },
    { name: "a cross-file class construction jumps to the module declaration", source: "import { Widget } from \"./util.cp3\";\nconst w = new __CURSOR__Widget();\n", modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]], expectLines: [0] },
    { name: "a cross-file interface in a parameter jumps to the module declaration", source: "import { Sized } from \"./util.cp3\";\nfunction f(s: __CURSOR__Sized): void {}\n", modules: [["util.cp3", "export interface Sized {\n  size(): number;\n}\n"]], expectLines: [0] },
    { name: "a cross-file enum jumps to the module declaration", source: "import { Mode } from \"./util.cp3\";\nconst m = __CURSOR__Mode.Fast;\n", modules: [["util.cp3", "export enum Mode { Fast, Slow }\n"]], expectLines: [0] },
    { name: "a cross-file type alias jumps to the module declaration", source: "import { Radians } from \"./util.cp3\";\nconst r: __CURSOR__Radians = 1;\n", modules: [["util.cp3", "export type Radians = number;\n"]], expectLines: [0] },
]);
