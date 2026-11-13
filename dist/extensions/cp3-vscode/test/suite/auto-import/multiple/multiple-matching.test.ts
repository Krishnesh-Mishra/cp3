// Auto-import / multiple matching exports
import { autoImportCases } from "../../harness";

autoImportCases("Auto-import: multiple matching exports", [
    {
        name: "identical exports in two modules both appear as candidates",
        source: "const joined = __CURSOR__\n",
        modules: [["a/util.cp3", "export function combine(a: string, b: string): string {\n  return a + b;\n}\n"],
                  ["b/util.cp3", "export function combine(x: string, y: string): string {\n  return x;\n}\n"]],
        expectOffered: "combine",
    },
    {
        name: "same-name exports in a near and far module offer the near one",
        source: "const joined = __CURSOR__\n",
        modules: [["near.cp3", "export function combine(a: string): string {\n  return a;\n}\n"],
                  ["far/deep/nested/near.cp3", "export function combine(x: string): string {\n  return x;\n}\n"]],
        expectOffered: "combine",
        expectImportInserted: "./near.cp3",
    },
    {
        name: "three modules exporting the same name stay all offered without duplicates",
        source: "const joined = __CURSOR__\n",
        modules: [["one.cp3", "export function merge(a: string): string { return a; }\n"],
                  ["two.cp3", "export function merge(b: string): string { return b; }\n"],
                  ["three.cp3", "export function merge(c: string): string { return c; }\n"]],
        expectOffered: "merge",
    },
    {
        name: "a function and a const with the same name are both candidates",
        source: "const joined = __CURSOR__\n",
        modules: [["fn.cp3", "export function mixed(a: string): string { return a; }\n"],
                  ["const.cp3", "export const mixed: string = \"mixed\";\n"]],
        expectOffered: "mixed",
    },
    {
        name: "nearest candidate wins the inserted import deterministically",
        source: "const joined = __CURSOR__\n",
        modules: [["shared/closest.cp3", "export function nearest(a: string): string { return a; }\n"],
                  ["elsewhere/deeply/nested/further.cp3", "export function nearest(b: string): string { return b; }\n"]],
        expectOffered: "nearest",
        expectImportInserted: "shared/closest.cp3",
    },
    {
        name: "the candidate with the deeper path still carries an import edit",
        source: "const joined = __CURSOR__\n",
        modules: [["shallow.cp3", "export function deepOnly(a: string): string { return a; }\n"],
                  ["deep/path/for/deepOnly.cp3", "export function deepOnly(b: string): string { return b; }\n"]],
        expectOffered: "deepOnly",
    },
    {
        name: "a class and a function with the same name offer distinct items",
        source: "const joined = __CURSOR__\n",
        modules: [["cls.cp3", "export class Combo { render(): void {} }\n"],
                  ["fn.cp3", "export function Combo(): string { return \"combo\"; }\n"]],
        expectOffered: "Combo",
    },
    {
        name: "an interface and a class with the same name are not conflated",
        source: "const joined = __CURSOR__\n",
        modules: [["iface.cp3", "export interface Twin { id: number; }\n"],
                  ["cls.cp3", "export class Twin { id: number = 1; }\n"]],
        expectOffered: "Twin",
    },
    {
        name: "export with the same name from two modules offers both without error",
        source: "const joined = __CURSOR__\n",
        modules: [["a.cp3", "export const shared: string = \"a\";\n"],
                  ["b.cp3", "export const shared: string = \"b\";\n"]],
        expectOffered: "shared",
    },
    {
        name: "a typo beyond prefix still offers the real candidate",
        source: "const joined = __CURSOR__\n",
        modules: [["a.cp3", "export function fuzzyMatch(a: string): string { return a; }\n"]],
        expectOffered: "fuzzyMatch",
    },
]);
