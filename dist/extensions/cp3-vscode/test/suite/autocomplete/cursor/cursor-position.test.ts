// Autocomplete / cursor-position edge cases
import { completionCases } from "../../harness";

completionCases("Autocomplete: cursor-position edge cases", [
    {
        name: "cursor at end of line after the dot",
        source: "const user = { name: \"Test\" };\nuser.__CURSOR__\n",
        expectPresent: ["name"],
    },
    {
        name: "cursor right before an existing member name",
        source: "const user = { name: \"Test\", age: 25, nickname: \"nk\" };\nuser.na__CURSOR__ame;\n",
        expectPresent: ["name", "nickname"],
        expectAbsent: ["age"],
    },
    {
        name: "cursor in the middle of a prefix keeps filtering",
        source: "const signal = 1;\nconst silence = 2;\nconst value = sig__CURSOR__\n",
        expectPresent: ["signal"],
        expectAbsent: ["silence"],
    },
    {
        name: "cursor on an empty line offers nothing structural",
        source: "function main(): void {\n\n  __CURSOR__\n}\n",
        expectPresent: ["return", "const"],
    },
    {
        name: "cursor after a newline still resolves the enclosing scope",
        source: "const shared = \"shared\";\n\nfunction read(): string {\n  return __CURSOR__;\n}\n",
        expectPresent: ["shared"],
    },
    {
        name: "cursor between two statements completes the upper one",
        source: "function main(): void {\n  const first = 1;\n  __CURSOR__\n  const second = 2;\n}\n",
        expectPresent: ["first"],
    },
    {
        name: "cursor inside a nested template literal interpolation",
        source: "const n = 3;\nconst label = `${n + __CURSOR__}`;\n",
        expectPresent: ["n"],
    },
    {
        name: "cursor in a comment does not leak completions into code",
        source: "const real = 1;\n// const fake = __CURSOR__\nconst after = real;\n",
        expectPresent: [],
    },
    {
        name: "cursor at end of file after an open brace",
        source: "function main(): void {\n  if (true) {\n    const scoped = 1;\n    const read = scoped;\n",
        expectPresent: [],
    },
    {
        name: "cursor at start of line inside a call argument",
        source: "function consume(value: number): void {}\nconst value = 7;\nconsume(\n  __CURSOR__\n);\n",
        expectPresent: ["value"],
    },
]);
