// Diagnostics / class implementation errors
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: class implementation errors", [
    {
        name: "abstract class cannot be instantiated directly",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nconst s = new Shape();\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "subclass that does not implement the abstract member is an error",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends Shape {\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "subclass that does implement the abstract member is not an error",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends Shape {\n  area(): number { return 16; }\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "abstract class instantiation error reports on the new expression",
        source: "abstract class Widget {\n  abstract render(): void;\n}\nconst w = new Widget();\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a class field with the wrong type in its implementation is an error",
        source: "class Machine {\n  count: number = 0;\n}\nconst m = new Machine();\nm.count = \"many\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "an assigned field not declared in the class is an error",
        source: "class Machine {\n  count: number = 0;\n}\nconst m = new Machine();\nm.missing = 1;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "overriding a number member with a string implementation is an error",
        source: "class Base {\n  size: number = 1;\n}\nclass Derived extends Base {\n  size: string = \"big\";\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a class cannot implement its own interface incorrectly",
        source: "interface Sizable {\n  size: number;\n}\nclass Crate implements Sizable {\n  size: string = \"large\";\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "constructor parameter count mismatch is an error at the call site",
        source: "class Pair {\n  constructor(a: number, b: number) {\n  }\n}\nconst p = new Pair(1);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "static member accessed through an instance is an error",
        source: "class Machine {\n  static counter: number = 0;\n}\nconst m = new Machine();\nconst value = m.counter;\n",
        expect: [{ severity: "error" }],
    },
]);
