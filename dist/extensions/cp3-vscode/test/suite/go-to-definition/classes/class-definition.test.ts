// Go to definition / classes
import { definitionCases } from "../../harness";

definitionCases("Go to definition: classes", [
    {
        name: "a constructor call jumps to the class declaration",
        source: "class Counter {\n  count: number = 0;\n}\nconst c = new __CURSOR__Counter();\n",
        expectLines: [0],
    },
    {
        name: "a type annotation position jumps to the class declaration",
        source: "class Widget {}\nfunction attach(w: __CURSOR__Widget): void {}\n",
        expectLines: [0],
    },
    {
        name: "an extends clause jumps to the base class",
        source: "class Base {}\nclass Derived extends __CURSOR__Base {}\n",
        expectLines: [0],
    },
    {
        name: "a class static member access jumps to the static declaration",
        source: "class Counter {\n  static limit: number = 10;\n}\nconst cap = Counter.__CURSOR__limit;\n",
        expectLines: [1],
    },
    {
        name: "a class method call jumps to the method declaration",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__start();\n",
        expectLines: [1],
    },
    {
        name: "an instance property jumps to the property declaration",
        source: "class Machine {\n  speed: number = 0;\n}\nconst m = new Machine();\nconst s = m.__CURSOR__speed;\n",
        expectLines: [1],
    },
    {
        name: "an abstract class jumps to its declaration from a subclass",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends __CURSOR__Shape {\n  area(): number { return 1; }\n}\n",
        expectLines: [0],
    },
    {
        name: "a generic class jumps to its declaration from the type argument",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new __CURSOR__Box<number>();\n",
        expectLines: [0],
    },
    {
        name: "a class used across two files jumps into the defining file",
        source: "import { Widget } from \"./util.cp3\";\nconst w = new __CURSOR__Widget();\n",
        modules: [["util.cp3", "export class Widget {\n  render(): void {}\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a default exported class jumps to its declaration",
        source: "import Gadget from \"./util.cp3\";\nconst g = new __CURSOR__Gadget();\n",
        modules: [["util.cp3", "export default class Gadget {\n  fire(): void {}\n}\n"]],
        expectLines: [0],
    },
]);
