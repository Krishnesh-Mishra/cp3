// Signature help / classes and constructors
import { signatureHelpCases } from "../../harness";

signatureHelpCases("Signature help: classes", [
    {
        name: "a constructor call shows the constructor signature",
        source: "class Widget {\n  constructor(size: number) {\n  }\n}\nconst w = new Widget(__CURSOR__10);\n",
        contains: ["Widget", "size"],
    },
    {
        name: "a constructor with two parameters shows both",
        source: "class Point {\n  constructor(x: number, y: number) {\n  }\n}\nconst p = new Point(1, __CURSOR__2);\n",
        expectActiveParameter: 1,
        contains: ["x", "y"],
    },
    {
        name: "an explicit class constructor of a generic class shows its parameters",
        source: "class Box<T> {\n  value: T = 0 as T;\n  constructor(initial: T) {\n    this.value = initial;\n  }\n}\nconst b = new Box(__CURSOR__5);\n",
        contains: ["initial"],
    },
    {
        name: "a class method signature shows the method parameters",
        source: "class Machine {\n  move(dx: number, dy: number): void {}\n}\nconst m = new Machine();\nm.move(1, __CURSOR__2);\n",
        expectActiveParameter: 1,
        contains: ["move", "dx", "dy"],
    },
    {
        name: "a class getter-style call shows no parameters",
        source: "class Machine {\n  get speed(): number { return 1; }\n}\nconst m = new Machine();\nconst s = m.__CURSOR__speed;\n",
        expectNone: true,
    },
    {
        name: "a method with a documented signature shows the method docs",
        source: "class Counter {\n  /** Adds an amount to the counter. */\n  add(amount: number): void {}\n}\nconst c = new Counter();\nc.__CURSOR__add(3);\n",
        contains: ["add"],
    },
    {
        name: "a static constructor-style method shows the function signature",
        source: "class Factory {\n  static make(size: number, label: string): void {}\n}\nFactory.__CURSOR__make(1, \"x\");\n",
        expectActiveParameter: 0,
        contains: ["make", "size"],
    },
    {
        name: "a static method second parameter is active after the comma",
        source: "class Factory {\n  static make(size: number, label: string): void {}\n}\nFactory.make(1, __CURSOR__\"x\");\n",
        expectActiveParameter: 1,
        contains: ["label"],
    },
    {
        name: "a class used as a parameter shows the class in the signature",
        source: "class Stamp {}\nfunction apply(stamp: Stamp, label: string): void {}\nconst s = new Stamp();\napply(s, __CURSOR__\"x\");\n",
        expectActiveParameter: 1,
        contains: ["label"],
    },
    {
        name: "a two-object parameter call tracks the second parameter",
        source: "class Blob {}\nclass Batch {}\nfunction run(first: Blob, second: Batch): void {}\nconst b = new Blob();\nconst batch = new Batch();\nrun(b, __CURSOR__batch);\n",
        expectActiveParameter: 1,
        contains: ["second"],
    },
]);
