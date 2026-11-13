// Autocomplete / class and interface completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: class and interface completion", [
    {
        name: "class declared above is offered in expression position",
        source: "class Shape {}\nconst s = new __CURSOR__();\n",
        expectPresent: ["Shape"],
    },
    {
        name: "interface declared above is offered in type position",
        source: "interface Shape { kind: string; }\nfunction area(s: __CURSOR__): number { return 0; }\n",
        expectPresent: ["Shape"],
    },
    {
        name: "class static member is offered on the class name",
        source: "class Counter {\n  static step: number = 1;\n}\nCounter.__CURSOR__\n",
        expectPresent: ["step"],
    },
    {
        name: "class static function is offered on the class name",
        source: "class Counter {\n  static increment(): void {}\n}\nCounter.__CURSOR__\n",
        expectPresent: ["increment"],
    },
    {
        name: "class instance offers both property and method",
        source: "class Player {\n  score: number = 0;\n  reset(): void {}\n}\nconst p = new Player();\np.__CURSOR__\n",
        expectPresent: ["score", "reset"],
    },
    {
        name: "interface property is offered on the implementing value",
        source: "interface Vehicle {\n  maxSpeed: number;\n}\nclass Car implements Vehicle {\n  maxSpeed: number = 180;\n}\nconst c = new Car();\nc.__CURSOR__\n",
        expectPresent: ["maxSpeed"],
    },
    {
        name: "subclass offers inherited member",
        source: "class Base {\n  tag: string = \"base\";\n}\nclass Derived extends Base {}\nconst d = new Derived();\nd.__CURSOR__\n",
        expectPresent: ["tag"],
    },
    {
        name: "abstract class is offered in type position",
        source: "abstract class Shape { abstract area(): number; }\nconst mover = (s: __CURSOR__): void => {};\n",
        expectPresent: ["Shape"],
    },
    {
        name: "interface method completion is offered on the value",
        source: "interface Logger {\n  log(message: string): void;\n}\nfunction emit(logger: Logger): void {\n  logger.__CURSOR__\n}\n",
        expectPresent: ["log"],
    },
    {
        name: "class type annotation position offers the class",
        source: "class Widget {}\nconst w: __CURSOR__\n",
        expectPresent: ["Widget"],
    },
    {
        name: "enum is offered in type position",
        source: "enum Direction { North, South, East, West }\nfunction travel(d: __CURSOR__): void {}\n",
        expectPresent: ["Direction"],
    },
    {
        name: "enum is offered in value position",
        source: "enum Direction { North, South }\nconst head = __CURSOR__;\n",
        expectPresent: ["Direction"],
    },
    {
        name: "type alias is offered in type position",
        source: "type Radians = number;\nfunction toDegrees(r: __CURSOR__): number { return 0; }\n",
        expectPresent: ["Radians"],
    },
    {
        name: "generic class is offered with its name in value position",
        source: "class Box<T> {\n  value: T = 0 as T;\n}\nconst b = new __CURSOR__();\n",
        expectPresent: ["Box"],
    },
    {
        name: "implementing two interfaces keeps both property sets",
        source: "interface HasName { name: string; }\ninterface HasAge { age: number; }\nclass Person implements HasName, HasAge {\n  name: string = \"p\";\n  age: number = 1;\n}\nconst p = new Person();\np.__CURSOR__\n",
        expectPresent: ["name", "age"],
    },
]);
