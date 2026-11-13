// Autocomplete / variable completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: variable completion", [
    {
        name: "local declared above is offered in the same scope",
        source: "function main(): number {\n  const total = 10;\n  return __CURSOR__;\n}\n",
        expectPresent: ["total"],
    },
    {
        name: "several locals are all offered",
        source: "function main(): number {\n  const alpha = 1;\n  let beta = 2;\n  return __CURSOR__;\n}\n",
        expectPresent: ["alpha", "beta"],
    },
    {
        name: "parameter is offered in the function body",
        source: "function greet(name: string): void {\n  return __CURSOR__;\n}\n",
        expectPresent: ["name"],
    },
    {
        name: "outer local is offered inside a nested block",
        source: "function main(): number {\n  const outer = 7;\n  if (true) {\n    return __CURSOR__;\n  }\n  return 0;\n}\n",
        expectPresent: ["outer"],
    },
    {
        name: "outer local is offered inside a nested function",
        source: "function main(): number {\n  const captured = 42;\n  function inner(): number {\n    return __CURSOR__;\n  }\n  return inner();\n}\n",
        expectPresent: ["captured"],
    },
    {
        name: "block-scoped variable is offered right after declaration",
        source: "function main(): number {\n  if (true) {\n    const scoped = 3;\n    return __CURSOR__;\n  }\n  return 0;\n}\n",
        expectPresent: ["scoped"],
    },
    {
        name: "arrow function parameter is offered in arrow body",
        source: "const dbl = (item: number): number => {\n  return __CURSOR__;\n};\n",
        expectPresent: ["item"],
    },
    {
        name: "string local offers string methods",
        source: "const fruit = \"apple\";\nfruit.__CURSOR__\n",
        expectPresent: ["toUpperCase"],
    },
    {
        name: "string local offers toLowerCase",
        source: "const fruit = \"apple\";\nfruit.__CURSOR__\n",
        expectPresent: ["toLowerCase"],
    },
    {
        name: "string local is filtered to string members only",
        source: "const fruit = \"apple\";\nfruit.__CURSOR__\n",
        expectAbsent: ["push", "map", "join"],
    },
    {
        name: "number local is offered in arithmetic",
        source: "const a = 5;\nconst b = __CURSOR__;\n",
        expectPresent: ["a"],
    },
    {
        name: "const rebinding keeps offering the name",
        source: "const counter = 1;\nconst other = __CURSOR__;\n",
        expectPresent: ["counter"],
    },
    {
        name: "variable declared in an enclosing loop is offered in the loop body",
        source: "function main(): number {\n  for (let i = 0; i < 3; i += 1) {\n    const seen = i * 2;\n    print(__CURSOR__);\n  }\n  return 0;\n}\n",
        expectPresent: ["seen", "i"],
    },
    {
        name: "const array offers array methods",
        source: "const values = [1, 2, 3];\nvalues.__CURSOR__\n",
        expectPresent: ["join", "push", "map"],
    },
    {
        name: "array of strings offers string member via element indexing",
        source: "const names = [\"a\", \"b\"];\nnames[0].__CURSOR__\n",
        expectPresent: ["toUpperCase"],
    },
]);
