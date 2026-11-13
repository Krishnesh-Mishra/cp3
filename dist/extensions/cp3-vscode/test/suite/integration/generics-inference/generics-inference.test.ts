// Integration / generics and inference
import { completionCases, hoverCases } from "../../harness";

completionCases("Integration: generics + inference (completion)", [
    { name: "identity<T> infers T from a number argument and keeps number members", source: "function identity<T>(v: T): T { return v; }\nconst x = identity(1);\nx.__CURSOR__\n", expectPresent: ["toFixed"] },
    { name: "identity<T> infers T from a string argument and keeps string members", source: "function identity<T>(v: T): T { return v; }\nconst x = identity(\"hi\");\nx.__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "identity<T> on an array infers the element type members", source: "function identity<T>(v: T): T { return v; }\nconst x = identity([1, 2]);\nx.__CURSOR__\n", expectPresent: ["join"] },
    { name: "identity<T> on a boolean infers boolean members", source: "function identity<T>(v: T): T { return v; }\nconst x = identity(true);\nx.__CURSOR__\n", expectAbsent: ["push", "toUpperCase"] },
    { name: "box<T>(\"s\").item keeps string members", source: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<string>();\nb.value.__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "box<T>(1).item does not offer string members", source: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<number>();\nb.value.__CURSOR__\n", expectAbsent: ["toUpperCase"] },
    { name: "a generic function with a default infers from the default", source: "function wrap<T = string>(v: T): T { return v; }\nconst out = wrap(\"s\");\nout.__CURSOR__\n", expectPresent: ["toLowerCase"] },
    { name: "a map call preserves element types on the result", source: "const xs = [\"a\", \"b\"];\nconst mapped = xs.map((s) => s);\nmapped[0].__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "a filter call returns the same element type on the result", source: "const xs = [1, 2, 3];\nconst evens = xs.filter((n) => n % 2 === 0);\nevens[0].__CURSOR__\n", expectAbsent: ["toUpperCase"] },
    { name: "a reduce accumulator infers from the initial value", source: "const xs = [1, 2, 3];\nconst total = xs.reduce((acc, n) => acc + n, 0);\nconst read = __CURSOR__total;\n", expectPresent: [] },
    { name: "instantiation with no arguments fails gracefully at the type position", source: "function f<T>(v: T): T { return v; }\nconst x: __CURSOR__f = f;\n", expectPresent: [] },
    { name: "new with explicit generic argument narrows the class members", source: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<number>();\nb.value.__CURSOR__\n", expectAbsent: ["toUpperCase"] },
    { name: "a generic const declared with an explicit argument keeps the member type", source: "class Box<T> { value: T = 0 as T; }\nconst b: Box<string> = new Box<string>();\nb.value.__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "a pair<A, B> infers both type arguments independently", source: "class Pair<A, B> { first: A = 0 as A; second: B = 0 as B; }\nconst p = new Pair<number, string>();\np.second.__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "a pair<A, B> keeps the first argument type for its member", source: "class Pair<A, B> { first: A = 0 as A; second: B = 0 as B; }\nconst p = new Pair<number, string>();\np.first.__CURSOR__\n", expectAbsent: ["toUpperCase"] },
    { name: "class static from an instantiated generic shows member completion", source: "class Box<T> { value: T = 0 as T; }\nconst b = new Box<string>();\nb.value.__CURSOR__\n", expectPresent: ["toLowerCase"] },
]);

hoverCases("Integration: generics + inference (hover)", [
    { name: "identity<number> call hover reports number return", source: "function identity<T>(v: T): T { return v; }\nconst x = identity(__CURSOR__1);\n", contains: ["number"] },
    { name: "identity<string> call hover reports string return", source: "function identity<T>(v: T): T { return v; }\nconst x = identity(__CURSOR__\"hi\");\n", contains: ["string"] },
    { name: "box<string> hover reports the generic instantiation", source: "class Box<T> { value: T = 0 as T; }\nconst b = new __CURSOR__Box<string>();\n", contains: ["string"] },
    { name: "map inference keeps hover informative", source: "const xs = [1, 2, 3];\nconst doubled = xs.map((n) => n * 2);\ndoubled.__CURSOR__\n", contains: [] },
    { name: "await hover on a promise of number reports the inner type", source: "async function pending(): Promise<number> { return 1; }\nconst value = await __CURSOR__pending();\n", contains: ["number"] },
    { name: "a generic parameter hover reports the constraint when present", source: "interface Sized { size(): number }\nfunction biggest<T extends Sized>(a: T): T { return a; }\nfunction use(s: Sized) { return biggest(__CURSOR__s); }\n", contains: ["Sized"] },
    { name: "a generic array hover reports the element type", source: "const xs = [\"a\", \"b\"];\nconst read = __CURSOR__xs;\n", contains: ["string"] },
    { name: "a tuple-annotated hover stays stable", source: "const pair: [number, string] = [1, \"two\"];\nconst read = __CURSOR__pair;\n", contains: ["number"] },
    { name: "a nested generic hover stays stable", source: "class Box<T> { value: T = 0 as T; }\nconst nested = new Box(new Box(\"inner\"));\nconst read = __CURSOR__nested;\n", contains: ["Box"] },
    { name: "an instantiated method hover reports its parameter", source: "class Box<T> { describe(value: T): void {} }\nconst b = new Box<string>();\nb.__CURSOR__describe(\"x\");\n", contains: ["describe"] },
    { name: "an instantiated interface hover reports the interface", source: "interface Sized {}\nclass Impl implements Sized {}\nconst impl: __CURSOR__Sized = new Impl();\n", contains: ["Sized"] },
    { name: "a generic return call hover keeps the inner type", source: "function first<T>(list: T[]): T | undefined { return list[0]; }\nconst head = first(__CURSOR__[1, 2]);\n", contains: ["number"] },
    { name: "a generic singleton hover reports the argument type", source: "function singleton<T>(v: T): T { return v; }\nconst one = singleton(__CURSOR__\"solo\");\n", contains: ["string"] },
    { name: "a widened const hover reports the widened type", source: "const xs = [1, 2, 3] as number[];\nconst read = __CURSOR__xs;\n", contains: ["number"] },
    { name: "a member completion candidate hover reports the member", source: "const user = { name: \"Test\" };\nconst read = __CURSOR__user.name;\n", contains: ["name"] },
]);
