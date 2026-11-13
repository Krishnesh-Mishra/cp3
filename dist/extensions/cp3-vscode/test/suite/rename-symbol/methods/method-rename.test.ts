// Rename symbol / methods and classes
import { renameCases } from "../../harness";

renameCases("Rename symbol: methods and classes", [
    {
        name: "a method is renamed at its declaration and call site",
        source: "class Machine {\n  __CURSOR__start(): void {}\n}\nconst m = new Machine();\nm.start();\n",
        newName: "boot",
    },
    {
        name: "a static method is renamed at its declaration and use",
        source: "class Machine {\n  static __CURSOR__reset(): void {}\n}\nMachine.reset();\n",
        newName: "restart",
    },
    {
        name: "an instance method used twice renames at both call sites",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.start();\nm.__CURSOR__start();\n",
        newName: "boot",
        expectEditCount: 3,
    },
    {
        name: "a class is renamed across its declaration and constructor calls",
        source: "class Widget {}\nconst w = new __CURSOR__Widget();\nconst x = Widget;\n",
        newName: "Gadget",
        expectEditCount: 3,
    },
    {
        name: "a class extending another is renamed at both names",
        source: "class Base {}\nclass Derived extends __CURSOR__Base {}\nconst b = new Base();\n",
        newName: "Parent",
        expectEditCount: 3,
    },
    {
        name: "an abstract method is renamed at the abstract declaration and the implementation",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends Shape {\n  __CURSOR__area(): number { return 1; }\n}\n",
        newName: "surface",
    },
    {
        name: "a class referenced in a type annotation is renamed there too",
        source: "class Widget {}\nfunction attach(w: Widget): void {}\nconst w = new __CURSOR__Widget();\n",
        newName: "Panel",
        expectEditCount: 3,
    },
    {
        name: "a class exported and used in the same file renames both",
        source: "export class Button {}\nconst b = new __CURSOR__Button();\n",
        newName: "Key",
        expectEditCount: 2,
    },
    {
        name: "a method used inside another method of the same class is renamed",
        source: "class Worker {\n  helper(): void {}\n  run(): void {\n    helper();\n  }\n}\nconst w = new Worker();\nw.__CURSOR__helper();\n",
        newName: "assist",
        expectEditCount: 3,
    },
    {
        name: "renaming a class renames the type annotation usage as well",
        source: "class Crate {}\nconst items: Crate[] = [];\nconst c = new __CURSOR__Crate();\n",
        newName: "Box",
        expectEditCount: 3,
    },
]);
