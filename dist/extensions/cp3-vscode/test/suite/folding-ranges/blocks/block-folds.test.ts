// Folding ranges / declarations and blocks
import { foldingRangeCases } from "../../harness";

foldingRangeCases("Folding ranges: declarations and blocks", [
    {
        name: "a class folds its member block",
        source: "class Machine {\n  speed: number = 0;\n  start(): void {}\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "an interface folds its body",
        source: "interface Sized {\n  size(): number;\n  capacity(): number;\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "an enum folds its member list",
        source: "enum Direction {\n  North,\n  South,\n  East,\n  West,\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "a function folds its body",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "a nested function folds both bodies",
        source: "function outer(): void {\n  function inner(): void {\n    const x = 1;\n  }\n  inner();\n}\n",
        expectAtLeast: 2,
    },
    {
        name: "an if block folds inside a function",
        source: "function f(n: number): number {\n  if (n > 10) {\n    return 1;\n  }\n  return 0;\n}\n",
        expectAtLeast: 2,
    },
    {
        name: "a for loop folds inside a function",
        source: "function f(items: number[]): number {\n  let total = 0;\n  for (const item of items) {\n    total = total + item;\n  }\n  return total;\n}\n",
        expectAtLeast: 2,
    },
    {
        name: "a while loop folds inside a function",
        source: "function f(): number {\n  let n = 3;\n  while (n > 0) {\n    n = n - 1;\n  }\n  return n;\n}\n",
        expectAtLeast: 2,
    },
    {
        name: "a try block folds inside a function",
        source: "function f(): void {\n  try {\n    risky();\n  } catch (e) {\n    print(\"caught\");\n  } finally {\n    print(\"done\");\n  }\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "an object literal spanning lines folds as a block",
        source: "const config = {\n  retries: 3,\n  timeout: 100,\n};\n",
        expectAtLeast: 1,
    },
    {
        name: "a function with a single statement has a foldable range",
        source: "function f(): number {\n  return 1;\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "a default exported class folds its body",
        source: "export default class Widget {\n  value: number = 0;\n  render(): void {}\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "a generic class folds its body",
        source: "class Box<T> {\n  value: T = 0 as T;\n  current(): T {\n    return this.value;\n  }\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "an abstract class folds its body",
        source: "abstract class Shape {\n  abstract area(): number;\n  describe(): string {\n    return \"shape\";\n  }\n}\n",
        expectAtLeast: 1,
    },
    {
        name: "deep nesting reports at least three ranges",
        source: "function deep(): number {\n  if (true) {\n    while (true) {\n      for (let i = 0; i < 1; i += 1) {\n        return i;\n      }\n    }\n  }\n  return 0;\n}\n",
        expectAtLeast: 3,
    },
    {
        name: "a comment block above a declaration does not break folding",
        source: "// leading comment\nfunction f(): number {\n  return 1;\n}\n",
        expectAtLeast: 1,
    },
]);
