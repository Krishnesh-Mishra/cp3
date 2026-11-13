// Autocomplete / scope and visibility
import { completionCases } from "../../harness";

completionCases("Autocomplete: scope and visibility", [
    {
        name: "shadowed builtin keeps completing the local",
        source: "const Object = { name: \"Rahul\" };\nconst label = Object.__CURSOR__;\n",
        expectPresent: ["name"],
    },
    {
        name: "inner scope shadows outer let",
        source: "function main(): number {\n  const count = 100;\n  if (true) {\n    const count = 3;\n    return __CURSOR__;\n  }\n  return count;\n}\n",
        expectPresent: ["count"],
    },
    {
        name: "loop variable shadows nothing but is offered",
        source: "function main(): number {\n  for (let index = 0; index < 4; index += 1) {\n    return __CURSOR__;\n  }\n  return 0;\n}\n",
        expectPresent: ["index"],
    },
    {
        name: "closure captures function parameter",
        source: "function makeMultiplier(factor: number): (x: number) => number {\n  return (x) => x * factor;\n}\nconst m = makeMultiplier(2);\nconst result = m(__CURSOR__);\n",
        expectPresent: ["makeMultiplier"],
    },
    {
        name: "module level const is offered in a function",
        source: "const SHARED = \"shared\";\nfunction read(): string {\n  return __CURSOR__;\n}\n",
        expectPresent: ["SHARED"],
    },
    {
        name: "unrelated module-level name is not offered as the only candidate expectation",
        source: "const UNRELATED = 99;\nfunction read(): number {\n  const local = 1;\n  return __CURSOR__;\n}\n",
        expectPresent: ["local"],
    },
    {
        name: "class field is not offered as a bare name outside the class",
        source: "class Machine {\n  field: number = 1;\n}\nconst outside = __CURSOR__;\n",
        expectAbsent: ["field"],
    },
    {
        name: "parameter name is offered inside nested arrow",
        source: "function run(seed: number): void {\n  const inner = () => {\n    print(__CURSOR__);\n  };\n  inner();\n}\n",
        expectPresent: ["seed"],
    },
    {
        name: "function declaration after use is offered as a hoisted value",
        source: "const value = below();\nfunction below(): number { return 42; }\nconst second = __CURSOR__below();\n",
        expectPresent: ["below"],
    },
    {
        name: "const declared after use is not offered at the use site",
        source: "function main(): number {\n  return __CURSOR__;\n  const later = 1;\n}\n",
        expectAbsent: ["later"],
    },
    {
        name: "sibling function parameter is not offered inside another parameter list",
        source: "function pair(left: number, right: number): void {\n  const sum = left + right;\n  print(__CURSOR__);\n}\n",
        expectPresent: ["sum", "left", "right"],
    },
    {
        name: "local const with generic type keeps its own scope visibility",
        source: "function main(): void {\n  const values: number[] = [1, 2];\n  print(__CURSOR__);\n}\n",
        expectPresent: ["values"],
    },
    {
        name: "global console is offered at module scope",
        source: "import console from \"std/console\";\nconsole.__CURSOR__\n",
        expectPresent: ["log"],
    },
    {
        name: "exported declaration is offered in the declaring file",
        source: "export const EXPORTED_MARKER = 7;\nconst read = __CURSOR__;\n",
        expectPresent: ["EXPORTED_MARKER"],
    },
    {
        name: "destructured parameter is offered in the body",
        source: "function open({ path }: { path: string }): void {\n  print(__CURSOR__);\n}\n",
        expectPresent: ["path"],
    },
]);
