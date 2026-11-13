// Rename symbol / variables
import { renameCases } from "../../harness";

renameCases("Rename symbol: variables", [
    {
        name: "a const is renamed at its declaration and use",
        source: "const target = 10;\nconst read = __CURSOR__target + 1;\n",
        newName: "destination",
        expectEditCount: 2,
    },
    {
        name: "a const rename touches every use site",
        source: "const target = \"x\";\nconst a = __CURSOR__target;\nconst b = target;\nconst c = target;\n",
        newName: "moved",
        expectEditCount: 4,
    },
    {
        name: "a let declared inside a function is renamed within the function",
        source: "function main(): number {\n  let count = 0;\n  count = count + 1;\n  return __CURSOR__count;\n}\n",
        newName: "total",
    },
    {
        name: "a parameter is renamed at its declaration and inside the body",
        source: "function greet(name: string): string {\n  return __CURSOR__name;\n}\n",
        newName: "given",
        expectEditCount: 2,
    },
    {
        name: "a for-loop index is renamed at both sites",
        source: "function main(): number {\n  for (let i = 0; i < 3; i += 1) {\n    print(__CURSOR__i);\n  }\n  return 0;\n}\n",
        newName: "iteration",
    },
    {
        name: "a shadowed local is renamed without touching the outer variable",
        source: "const shared = 100;\nfunction main(): number {\n  const shared = 1;\n  return __CURSOR__shared;\n}\n",
        newName: "inner",
        expectEditCount: 2,
    },
    {
        name: "a module-scope const used across statements renames every time",
        source: "const LIMIT = 10;\nconst double = __CURSOR__LIMIT * 2;\nconst triple = LIMIT * 3;\nconst quad = LIMIT * 4;\n",
        newName: "CAP",
        expectEditCount: 4,
    },
    {
        name: "a variable used in a nested closure is renamed at the closure site",
        source: "function main(): number {\n  const base = 40;\n  const add = (): number => base + 2;\n  return __CURSOR__base;\n}\n",
        newName: "seed",
        expectEditCount: 3,
    },
    {
        name: "a variable whose new name contains the old one renames cleanly",
        source: "const count = 5;\nconst read = __CURSOR__count;\n",
        newName: "countdown",
        expectEditCount: 2,
    },
    {
        name: "renaming to an existing name in the same scope is still a legal rename",
        source: "const first = 1;\nconst second = first;\nconst third = __CURSOR__first + second;\n",
        newName: "renamedFirst",
        expectEditCount: 3,
    },
]);
