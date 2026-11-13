// Document formatting / idempotency on canonical sources
import { formattingCases } from "../../harness";

formattingCases("Document formatting: idempotency", [
    {
        name: "a formatted function formats to itself",
        source: "function f(): number {\n  const x = 1;\n  return x;\n}\n",
        expectText: "function f(): number {\n  const x = 1;\n  return x;\n}\n",
    },
    {
        name: "a formatted class formats to itself",
        source: "class Machine {\n  speed: number = 0;\n\n  boost(amount: number): void {\n    this.speed = this.speed + amount;\n  }\n}\n",
        expectText: "class Machine {\n  speed: number = 0;\n\n  boost(amount: number): void {\n    this.speed = this.speed + amount;\n  }\n}\n",
    },
    {
        name: "a formatted interface formats to itself",
        source: "interface Sized {\n  length: number;\n  capacity: number;\n}\n",
        expectText: "interface Sized {\n  length: number;\n  capacity: number;\n}\n",
    },
    {
        name: "a formatted enum formats to itself",
        source: "enum Direction {\n  North,\n  South,\n  East,\n  West,\n}\n",
        expectText: "enum Direction {\n  North,\n  South,\n  East,\n  West,\n}\n",
    },
    {
        name: "a formatted object literal formats to itself",
        source: "const config = {\n  retries: 3,\n  timeout: 100,\n  verbose: true,\n};\n",
        expectText: "const config = {\n  retries: 3,\n  timeout: 100,\n  verbose: true,\n};\n",
    },
    {
        name: "formatted nested control flow formats to itself",
        source: "function walk(items: number[]): number {\n  let total = 0;\n  for (const item of items) {\n    if (item > 0) {\n      total = total + item;\n    }\n  }\n  return total;\n}\n",
        expectText: "function walk(items: number[]): number {\n  let total = 0;\n  for (const item of items) {\n    if (item > 0) {\n      total = total + item;\n    }\n  }\n  return total;\n}\n",
    },
    {
        name: "formatted imports and const declarations format to themselves",
        source: "import Math from 'std/math';\nimport console from 'std/console';\n\nconst value = Math.abs(-1);\nconsole.log(`${value}`);\n",
        expectText: "import Math from 'std/math';\nimport console from 'std/console';\n\nconst value = Math.abs(-1);\nconsole.log(`${value}`);\n",
    },
    {
        name: "a formatted async function formats to itself",
        source: "async function load(): Promise<number> {\n  const value = 1;\n  return value + 1;\n}\n",
        expectText: "async function load(): Promise<number> {\n  const value = 1;\n  return value + 1;\n}\n",
    },
    {
        name: "a formatted type alias formats to itself",
        source: "type Radians = number;\nconst angle: Radians = 1;\n",
        expectText: "type Radians = number;\nconst angle: Radians = 1;\n",
    },
    {
        name: "formatted multi-item const declarations format to themselves",
        source: "const first = 1;\nconst second = 2;\nconst third = first + second;\n",
        expectText: "const first = 1;\nconst second = 2;\nconst third = first + second;\n",
    },
]);
