// Find references / class members and methods
import { referencesCases } from "../../harness";

referencesCases("Find references: class members", [
    {
        name: "a method referenced at the declaration and one call is found",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__start();\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [1],
    },
    {
        name: "a method referenced at two call sites is found at both",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__start();\nm.start();\n",
        expectCount: 3,
        includeDeclaration: true,
    },
    {
        name: "a static method referenced on the class is found",
        source: "class Machine {\n  static build(): void {}\n}\nMachine.__CURSOR__build();\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [1],
    },
    {
        name: "an instance property read twice is found at both sites",
        source: "class Machine {\n  speed: number = 0;\n}\nconst m = new Machine();\nconst a = m.__CURSOR__speed;\nconst b = m.speed;\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [1, 4, 5],
    },
    {
        name: "a property written and read is found at both access sites",
        source: "class Machine {\n  count: number = 0;\n}\nconst m = new Machine();\nm.__CURSOR__count = 5;\nconst read = m.count;\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [1, 4, 5],
    },
    {
        name: "a method shadowed in a subclass keeps one declaration reference",
        source: "class Base {\n  run(): void {}\n}\nclass Derived extends Base {\n  run(): void {}\n}\nconst a = new Base();\nconst b = new Derived();\na.__CURSOR__run();\nb.run();\n",
        expectCount: 3,
        includeDeclaration: true,
        expectSomeLines: [1],
    },
    {
        name: "a getter referenced on an object is found at the access site",
        source: "class Box {\n  get value(): number { return 1; }\n}\nconst b = new Box();\nconst read = b.__CURSOR__value;\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [1, 4],
    },
    {
        name: "a constructor referenced via new is found at the new expression",
        source: "class Widget {}\nconst w = new __CURSOR__Widget();\n",
        expectCount: 2,
        includeDeclaration: true,
        expectSomeLines: [0, 1],
    },
    {
        name: "a method referenced across two instances is found for both",
        source: "class Ping {\n  pong(): void {}\n}\nconst p = new Ping();\nconst q = new Ping();\np.__CURSOR__pong();\nq.pong();\n",
        expectCount: 4,
        includeDeclaration: true,
    },
    {
        name: "a method used only inside the class is found at the internal call site",
        source: "class Worker {\n  helper(): void {}\n  run(): void {\n    __CURSOR__helper();\n  }\n}\n",
        expectSomeLines: [1, 3],
        includeDeclaration: true,
    },
]);
