// Integration / interfaces and their implementations
import { completionCases, diagnosticCases, hoverCases } from "../../harness";

completionCases("Integration: interfaces + implementations (completion)", [
    { name: "an implemented interface member is offered on the value", source: "interface Sized {\n  size(): number;\n}\nclass Box implements Sized {\n  size(): number { return 1; }\n}\nconst b = new Box();\nb.__CURSOR__\n", expectPresent: ["size"] },
    { name: "an interface with two members offers both on the implementing value", source: "interface Twin {\n  left(): void;\n  right(): void;\n}\nclass Impl implements Twin {\n  left(): void {}\n  right(): void {}\n}\nconst i = new Impl();\ni.__CURSOR__\n", expectPresent: ["left", "right"] },
    { name: "an implementing class does not offer interface members absent from its shape", source: "interface Sized {\n  size(): number;\n  capacity(): number;\n}\nclass Box implements Sized {\n  size(): number { return 1; }\n  capacity(): number { return 2; }\n}\nconst b = new Box();\nb.__CURSOR__\n", expectAbsent: ["weight"] },
    { name: "an interface-typed const offers the interface members", source: "interface Config {\n  retries: number;\n}\nconst c: Config = { retries: 1 };\nc.__CURSOR__\n", expectPresent: ["retries"] },
    { name: "an interface-typed parameter offers its members", source: "interface Config {\n  timeout: number;\n}\nfunction f(c: Config): void {\n  c.__CURSOR__\n}\n", expectPresent: ["timeout"] },
    { name: "an interface-typed return value offers its members", source: "interface Config {\n  retries: number;\n}\nfunction f(): Config {\n  return { retries: 1 };\n}\nconst c = f();\nc.__CURSOR__\n", expectPresent: ["retries"] },
    { name: "a default-exported interface's members offer on the value", source: "export default interface Config {\n  retries: number;\n}\nconst c: Config = { retries: 1 };\nc.__CURSOR__\n", expectPresent: ["retries"] },
    { name: "an interface property misread as a missing key errors on the write", source: "interface Sized {\n  size: number;\n}\nconst s: Sized = { size: 1 };\ns.size = __CURSOR__;\n", expectPresent: [] },
    { name: "a generic interface member offers on the instantiated value", source: "interface Pair<A, B> {\n  first: A;\n  second: B;\n}\nconst p: Pair<string, number> = { first: \"a\", second: 1 };\np.first.__CURSOR__\n", expectPresent: ["toUpperCase"] },
    { name: "an interface with a method parameter offers its signature", source: "interface Runner {\n  run(speed: number): void;\n}\nclass Car implements Runner {\n  run(speed: number): void {}\n}\nconst c = new Car();\nc.__CURSOR__\n", expectPresent: ["run"] },
]);

hoverCases("Integration: interfaces + implementations (hover)", [
    { name: "interface hover on implements clause reports the interface", source: "interface Sized {\n  size(): number;\n}\nclass Box implements __CURSOR__Sized {\n  size(): number { return 1; }\n}\n", contains: ["Sized"] },
    { name: "interface method hover reports the interface method", source: "interface Sized {\n  size(): number;\n}\nfunction f(s: Sized): void {\n  s.__CURSOR__size();\n}\n", contains: ["size"] },
    { name: "interface member hover reports the member name", source: "interface Config {\n  retries: number;\n}\nfunction f(c: Config): void {\n  const r = c.__CURSOR__retries;\n}\n", contains: ["retries"] },
    { name: "implementing class hover on interface member reports the method", source: "interface Sized {\n  size(): number;\n}\nclass Impl implements Sized {\n  size(): number { return 1; }\n}\nconst i = new Impl();\ni.__CURSOR__size();\n", contains: ["size"] },
    { name: "a default-exported interface hover reports the interface", source: "export default interface Config {\n  retries: number;\n}\nconst c: __CURSOR__Config = { retries: 1 };\n", contains: ["Config"] },
    { name: "a generic interface hover reports the interface", source: "interface Pair<A, B> {\n  first: A;\n  second: B;\n}\nconst p: __CURSOR__Pair<number, string> = { first: 1, second: \"s\" };\n", contains: ["Pair"] },
    { name: "an interface extending another reports the base interface on hover", source: "interface Base {\n  id: number;\n}\ninterface Derived extends __CURSOR__Base {\n  extra: string;\n}\n", contains: ["Base"] },
    { name: "an interface method implemented reports the method", source: "interface Runner {\n  run(speed: number): void;\n}\nfunction f(r: Runner): void {\n  r.__CURSOR__run(1);\n}\n", contains: ["run"] },
]);

diagnosticCases("Integration: interfaces + implementations (diagnostics)", [
    {
        name: "a class missing an interface member is diagnosed",
        source: "interface Sized {\n  size: number;\n  capacity: number;\n}\nclass Impl implements Sized {\n  size: number = 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a class missing the interface method is diagnosed",
        source: "interface Runner {\n  run(): void;\n}\nclass Impl implements Runner {\n}\n",
        expect: [{ severity: "error" }],
    },
]);
