// Document symbols / nesting and member outline
import { documentSymbolCases } from "../../harness";

documentSymbolCases("Document symbols: nesting", [
    {
        name: "a class outlines its methods",
        source: "class Machine {\n  start(): void {}\n  stop(): void {}\n}\n",
        contains: ["Machine", "start", "stop"],
    },
    {
        name: "a class outlines its properties",
        source: "class Machine {\n  speed: number = 0;\n  label: string = \"x\";\n}\n",
        contains: ["Machine", "speed", "label"],
    },
    {
        name: "an interface outlines its members",
        source: "interface Sized {\n  size(): number;\n  capacity(): number;\n}\n",
        contains: ["Sized", "size", "capacity"],
    },
    {
        name: "a namespace-like module outlines nested declarations",
        source: "function outer(): void {\n  function inner(): void {}\n}\n",
        contains: ["outer", "inner"],
    },
    {
        name: "an enum outlines its members",
        source: "enum Direction { North, South, East }\n",
        contains: ["Direction", "North", "South", "East"],
    },
    {
        name: "a generic class outlines its members",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\n",
        contains: ["Box", "value"],
    },
    {
        name: "a class with a constructor outlines the constructor",
        source: "class Point {\n  constructor(x: number, y: number) {}\n}\n",
        contains: ["Point", "constructor"],
    },
    {
        name: "an interface with fields and methods outlines both",
        source: "interface Mixed {\n  name: string;\n  greet(): void;\n}\n",
        contains: ["Mixed", "name", "greet"],
    },
    {
        name: "nested classes outline both levels",
        source: "class Outer {\n  class Nested {\n    thing(): void {}\n  }\n}\n",
        contains: ["Outer", "Nested", "thing"],
    },
    {
        name: "an abstract class outlines its abstract members",
        source: "abstract class Shape {\n  area(): number { return 0; }\n  abstract describe(): string;\n}\n",
        contains: ["Shape", "area", "describe"],
    },
    {
        name: "a class accessor outlines its getter",
        source: "class Box {\n  get value(): number { return 1; }\n}\n",
        contains: ["Box", "value"],
    },
    {
        name: "a default-exported interface outlines its members",
        source: "export default interface Config {\n  retries: number;\n  timeout: number;\n}\n",
        contains: ["Config", "retries", "timeout"],
    },
    {
        name: "a module with many declarations outlines all of them",
        source: "const alpha = 1;\nconst beta = 2;\nfunction gamma(): number { return alpha + beta; }\nclass Delta {}\ninterface Epsilon { ok: boolean; }\nenum Zeta { On, Off }\ntype Eta = number;\n",
        contains: ["alpha", "beta", "gamma", "Delta", "Epsilon", "Zeta", "Eta"],
    },
    {
        name: "exported and unexported symbols both appear in the outline",
        source: "export function visible(): number { return 1; }\nfunction hidden(): number { return 2; }\n",
        contains: ["visible", "hidden"],
    },
    {
        name: "a single const outlines without nesting noise",
        source: "const value = 1;\n",
        contains: ["value"],
    },
]);
