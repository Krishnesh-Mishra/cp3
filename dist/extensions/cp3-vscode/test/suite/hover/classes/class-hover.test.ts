// Hover / classes, interfaces and inheritance
import { hoverCases } from "../../harness";

hoverCases("Hover: classes and interfaces", [
    {
        name: "a class hover in a new expression shows the class",
        source: "class Counter {}\nconst c = new __CURSOR__Counter();\n",
        contains: ["Counter"],
    },
    {
        name: "a class hover in a type annotation shows the class",
        source: "class Widget {}\nconst w: __CURSOR__Widget = new Widget();\n",
        contains: ["Widget"],
    },
    {
        name: "a class in an extends clause hover shows the base class",
        source: "class Base {}\nclass Derived extends __CURSOR__Base {}\n",
        contains: ["Base"],
    },
    {
        name: "an interface hover in an implements clause shows the interface",
        source: "interface Sized {\n  size: number;\n}\nclass Impl implements __CURSOR__Sized {\n  size: number = 1;\n}\n",
        contains: ["Sized"],
    },
    {
        name: "an interface hover in a parameter annotation shows the interface",
        source: "interface Config {\n  retries: number;\n}\nfunction load(c: __CURSOR__Config): void {}\n",
        contains: ["Config"],
    },
    {
        name: "a method hover shows its signature",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__start();\n",
        contains: ["start"],
    },
    {
        name: "a static method hover shows its signature",
        source: "class Machine {\n  static build(): void {}\n}\nMachine.__CURSOR__build();\n",
        contains: ["build"],
    },
    {
        name: "an abstract method hover shows the abstract signature",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends Shape {\n  area(): number { return 1; }\n}\nconst sq = new Square();\nsq.__CURSOR__area();\n",
        contains: ["area"],
    },
    {
        name: "a getter hover shows the getter signature",
        source: "class Box {\n  get value(): number { return 1; }\n}\nconst b = new Box();\nconst read = b.__CURSOR__value;\n",
        contains: ["value"],
    },
    {
        name: "a property hover shows the member type",
        source: "class Machine {\n  speed: number = 0;\n}\nconst m = new Machine();\nconst s = m.__CURSOR__speed;\n",
        contains: ["speed"],
    },
]);
