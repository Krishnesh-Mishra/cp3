// Diagnostics / type mismatches
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: type mismatches", [
    {
        name: "assigning a string to a number variable is an error",
        source: "let x: number = 1;\nx = \"bad\";\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "typed assignment to a number const fails with a string literal",
        source: "const x: number = \"s\";\n",
        expect: [{ severity: "error", line: 1 }],
    },
    {
        name: "const annotation mismatch reports type mismatch",
        source: "let x: number = 1;\nx = \"bad\";\n",
        expect: [{ severity: "error", contains: "type mismatch" }],
    },
    {
        name: "string literal assigned to boolean const is an error",
        source: "const flag: boolean = \"yes\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "number literal assigned to string const is an error",
        source: "const s: string = 42;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "function returning a number cannot return a string",
        source: "function f(): number {\n  return \"nope\";\n}\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "function returning a string cannot return a number",
        source: "function f(): string {\n  return 42;\n}\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "boolean return type cannot return a number",
        source: "function f(): boolean {\n  return 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "void function cannot return a value",
        source: "function f(): void {\n  return 42;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "parameter of the wrong type is rejected at the call site",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst total = add(\"one\", \"two\");\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "number argument passed where string is expected is rejected",
        source: "function greet(name: string): void {}\ngreet(42);\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "string argument passed where number is expected is rejected",
        source: "function bump(n: number): void {}\nbump(\"one\");\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "incompatible object-property assignment is an error",
        source: "const Obj = { name: \"Rahul\", age: 25 };\nObj.name = \"Asha\";\nObj.age = \"twenty-five\";\n",
        expect: [{ severity: "error", line: 3 }],
    },
    {
        name: "rebinding a const is an error",
        source: "const stable = \"keep\";\nstable = \"changed\";\n",
        expect: [{ severity: "error", line: 2 }],
    },
    {
        name: "rebinding a const in a block is an error",
        source: "const frozen = 10;\nif (true) {\n  frozen = 20;\n}\n",
        expect: [{ severity: "error", line: 3 }],
    },
    {
        name: "interface member with a wrong value type is rejected",
        source: "interface Sized {\n  length: number;\n}\nconst s: Sized = { length: \"ten\" };\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "generic field assigned the wrong type is an error",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new Box<number>();\nb.value = \"str\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "boolean where number expected in a return is an error",
        source: "function read(): number {\n  return true;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "assigning a number to a string binary operand is an error",
        source: "const a = \"left \" + 5;\nconst b = a + true;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "null literal assigned to a typed boolean is an error",
        source: "const ok: boolean = null;\n",
        expect: [{ severity: "error" }],
    },
]);
