// Autocomplete / generic completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: generic completion", [
    {
        name: "generic class name is offered in type position argument list",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nfunction unwrap(box: Box<__CURSOR__>): void {}\n",
        expectPresent: ["number", "string"],
    },
    {
        name: "generic interface offers its name in type position",
        source: "interface Pair<A, B> {\n  first: A;\n  second: B;\n}\nfunction inspect(p: __CURSOR__): void {}\n",
        expectPresent: ["Pair"],
    },
    {
        name: "generic function offers its name at the call site",
        source: "function identity<T>(value: T): T { return value; }\nconst a = __CURSOR__;\n",
        expectPresent: ["identity"],
    },
    {
        name: "generic function type argument position offers primitives",
        source: "function identity<T>(value: T): T { return value; }\nconst a = identity<__CURSOR__>(1);\n",
        expectPresent: ["number", "string"],
    },
    {
        name: "generic return position offers the type parameter",
        source: "function identity<T>(value: T): T {\n  return __CURSOR__;\n}\n",
        expectPresent: ["value"],
    },
    {
        name: "constructed generic instance returns the argument type member",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new Box<number>();\nb.__CURSOR__\n",
        expectPresent: ["value"],
    },
    {
        name: "generic map over numbers offers array methods on the result",
        source: "const nums = [1, 2, 3];\nconst doubled = nums.map((n) => n * 2);\ndoubled.__CURSOR__\n",
        expectPresent: ["join", "map", "filter"],
    },
    {
        name: "generic constraint position keeps the constraint name in scope",
        source: "interface Sized { size(): number; }\nfunction biggest<T extends Sized>(a: T, b: T): T {\n  return __CURSOR__;\n}\n",
        expectPresent: ["a", "b"],
    },
    {
        name: "generic arrow function name is offered",
        source: "const pluck = <T>(list: T[], index: number): T => list[index];\nconst value = __CURSOR__;\n",
        expectPresent: ["pluck"],
    },
    {
        name: "generic instance member is offered with the substituted type",
        source: "class Wrapper<T> {\n  current: T = 0 as T;\n}\nconst w = new Wrapper<string>();\nw.__CURSOR__\n",
        expectPresent: ["current", "toUpperCase"],
    },
]);
