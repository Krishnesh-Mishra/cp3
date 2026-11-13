// Find references / local and module-scope symbols
import { referencesCases } from "../../harness";

referencesCases("Find references: local symbols", [
    {
        name: "a function is referenced as declaration and two call sites",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = __CURSOR__add(1, 2);\nconst other = add(3, 4);\n",
        expectCount: 3,
        expectSomeLines: [0, 3, 4],
        includeDeclaration: true,
    },
    {
        name: "a function is referenced once at the call site without the declaration",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = __CURSOR__add(1, 2);\n",
        expectCount: 1,
        expectSomeLines: [3],
        includeDeclaration: false,
    },
    {
        name: "excluding the declaration drops the decl reference",
        source: "const stable = 10;\nconst read = __CURSOR__stable;\n",
        expectCount: 1,
        expectSomeLines: [1],
        includeDeclaration: false,
    },
    {
        name: "a const used twice on one line yields two references",
        source: "const base = 7;\nconst double = __CURSOR__base + base;\n",
        expectCount: 2,
        expectSomeLines: [1],
        includeDeclaration: false,
    },
    {
        name: "a const used three times is found at every use site",
        source: "const shared = \"x\";\nconst a = __CURSOR__shared;\nconst b = shared;\nconst c = shared;\n",
        expectCount: 3,
        includeDeclaration: false,
    },
    {
        name: "a function referenced in multiple files is found across the workspace",
        source: "import { helper } from \"./util.cp3\";\nconst value = __CURSOR__helper();\nhelper();\n",
        modules: [["util.cp3", "export function helper(): number { return 1; }\n"]],
        expectCount: 4,
        includeDeclaration: true,
    },
    {
        name: "a class referenced in extends and type positions is found",
        source: "class Shape {}\nclass Square extends __CURSOR__Shape {}\nconst shapes: Shape[] = [new Square()];\n",
        expectCount: 2,
        includeDeclaration: false,
        expectSomeLines: [1, 2],
    },
    {
        name: "a local variable referenced twice on one line is found twice",
        source: "function main(): number {\n  const total = 1;\n  const again = __CURSOR__total + total;\n  return again;\n}\n",
        expectCount: 2,
        expectSomeLines: [2],
        includeDeclaration: false,
    },
    {
        name: "an unused local reports no references outside its declaration",
        source: "function main(): number {\n  const __CURSOR__unused = 42;\n  return 0;\n}\n",
        expectNone: true,
        includeDeclaration: false,
    },
    {
        name: "a property referenced on an object is found at every access site",
        source: "const obj = { value: 1, other: 2 };\nconst read = obj.__CURSOR__value;\nconst twice = obj.value;\n",
        expectSomeLines: [1, 2],
        includeDeclaration: false,
    },
]);
