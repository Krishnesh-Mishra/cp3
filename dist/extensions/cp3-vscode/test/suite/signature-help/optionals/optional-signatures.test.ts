// Signature help / optionals and generics
import { signatureHelpCases } from "../../harness";

signatureHelpCases("Signature help: optionals and generics", [
    {
        name: "an optional parameter call without it still shows the signature",
        source: "function greet(name: string, suffix?: string): string {\n  return suffix ? name + suffix : name;\n}\nconst out = greet(__CURSOR__\"Asha\");\n",
        contains: ["greet", "name"],
    },
    {
        name: "a supplied optional second parameter is tracked as the second parameter",
        source: "function greet(name: string, suffix?: string): string {\n  return suffix ? name + suffix : name;\n}\nconst out = greet(\"Asha\", __CURSOR__\"!\");\n",
        expectActiveParameter: 1,
        contains: ["suffix"],
    },
    {
        name: "a call with a wrong later argument still shows the first parameter active",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst out = add(__CURSOR__\"one\", 2);\n",
        expectActiveParameter: 0,
        contains: ["add"],
    },
    {
        name: "a generic function shows the type-erased parameter list",
        source: "function identity<T>(value: T): T {\n  return value;\n}\nconst out = identity(__CURSOR__42);\n",
        contains: ["identity", "value"],
    },
    {
        name: "a generic function with two parameters tracks both",
        source: "function pair<A, B>(first: A, second: B): string {\n  return \"pair\";\n}\nconst out = pair(1, __CURSOR__\"two\");\n",
        expectActiveParameter: 1,
        contains: ["second"],
    },
    {
        name: "a default-valued parameter is shown in the signature",
        source: "function connect(host: string, port: number = 8080): void {}\nconnect(\"local\", __CURSOR__3000);\n",
        contains: ["port"],
    },
    {
        name: "a default-valued parameter omitted still shows the signature",
        source: "function connect(host: string, port: number = 8080): void {}\nconnect(__CURSOR__\"local\");\n",
        expectActiveParameter: 0,
        contains: ["host"],
    },
    {
        name: "an overload-style call of a variadic shows the variadic list",
        source: "function logAll(...items: string[]): void {}\nlogAll(__CURSOR__\"a\");\n",
        contains: ["items"],
    },
    {
        name: "a variadic call tracks the rest parameter",
        source: "function logAll(...items: string[]): void {}\nlogAll(\"a\", __CURSOR__\"b\", \"c\");\n",
        contains: ["items"],
    },
    {
        name: "a nested multi-argument call tracks the inner call",
        source: "function add(a: number, b: number): number {\n  return a + b;\n}\nconst out = add(add(1, __CURSOR__2), 3);\n",
        expectActiveParameter: 1,
        contains: ["add"],
    },
]);
