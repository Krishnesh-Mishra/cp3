// Diagnostics / generic type errors
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: generic type errors", [
    {
        name: "a generic field assigned the wrong type is an error",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new Box<number>();\nb.value = \"str\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a generic string field assigned a number is an error",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new Box<string>();\nb.value = 42;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "two generic arguments on one class cannot cross over",
        source: "class Pair<A, B> {\n  first: A = 0 as A;\n  second: B = 0 as B;\n}\nconst p = new Pair<number, string>();\np.second = 99;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "generic function returning the wrong type is an error",
        source: "function identity<T>(value: T): T {\n  return \"nope\" as T;\n}\nconst r = identity<number>(1);\nr = \"wrong\";\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a generic function cannot return the wrong narrowed type",
        source: "function first<T>(list: T[]): T | undefined {\n  return undefined;\n}\nconst n = first([1, 2, 3]);\nconst check: number = n;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "generic array element assignment is type checked",
        source: "const xs: number[] = [1, 2, 3];\nxs.push(\"four\");\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "map producing the wrong element type is an error",
        source: "const xs = [1, 2, 3];\nconst ys = xs.map((x) => `label: ${x}`);\nconst typed: number[] = ys;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "generic constraint satisfied by the parameter keeps the call valid",
        source: "interface Sized {\n  size(): number;\n}\nclass Bucket implements Sized {\n  size(): number { return 1; }\n}\nfunction measure<T extends Sized>(value: T): number {\n  return value.size();\n}\nconst b = new Bucket();\nmeasure(b);\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "generic constraint violated at the call site is an error",
        source: "interface Sized {\n  size(): number;\n}\nfunction measure<T extends Sized>(value: T): number {\n  return value.size();\n}\nconst result = measure({ size: 1 });\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a generic class instantiated with an incompatible argument is an error",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nfunction takeBox(box: Box<number>): void {}\ntakeBox(new Box<string>());\n",
        expect: [{ severity: "error" }],
    },
]);
