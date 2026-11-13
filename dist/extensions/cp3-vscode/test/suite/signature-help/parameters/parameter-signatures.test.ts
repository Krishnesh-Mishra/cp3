// Signature help / parameters and active parameter tracking
import { signatureHelpCases } from "../../harness";

signatureHelpCases("Signature help: parameters", [
    {
        name: "the callee label appears when inside the argument list",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, __CURSOR__2);\n",
        contains: ["add"],
    },
    {
        name: "signature shows both parameters",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, __CURSOR__2);\n",
        contains: ["a", "b"],
    },
    {
        name: "signature shows the parameter types",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, __CURSOR__2);\n",
        contains: ["number"],
    },
    {
        name: "the first parameter is active right after the opening paren",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(__CURSOR__1, 2);\n",
        expectActiveParameter: 0,
        contains: ["add"],
    },
    {
        name: "the second parameter is active after the comma",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, __CURSOR__2);\n",
        expectActiveParameter: 1,
        contains: ["add"],
    },
    {
        name: "the signature help is tracked across both arguments",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(1, 2__CURSOR__);\n",
        expectActiveParameter: 1,
        contains: ["b"],
    },
    {
        name: "a three-parameter call tracks the third parameter",
        source: "function combine(a: string, b: string, c: string): string {\n  return a + b + c;\n}\nconst joined = combine(\"x\", \"y\", __CURSOR__\"z\");\n",
        expectActiveParameter: 2,
        contains: ["combine"],
    },
    {
        name: "a mixed-type signature shows each parameter's type",
        source: "function render(title: string, count: number, active: boolean): void {}\nrender(\"t\", 3, __CURSOR__true);\n",
        expectActiveParameter: 2,
        contains: ["boolean"],
    },
    {
        name: "a string parameter active after the second argument starts typing",
        source: "function repeat(text: string, times: number): string {\n  return text.repeat(times);\n}\nconst out = repeat(\"ab\", __CURSOR__3);\n",
        expectActiveParameter: 1,
        contains: ["repeat"],
    },
    {
        name: "signature help shows the full callee of a method call",
        source: "class Counter {\n  add(a: number, b: number): number {\n    return a + b;\n  }\n}\nconst c = new Counter();\nconst out = c.add(1, __CURSOR__2);\n",
        contains: ["add"],
    },
]);
