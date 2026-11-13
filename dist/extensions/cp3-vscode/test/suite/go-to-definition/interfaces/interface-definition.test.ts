// Go to definition / interfaces
import { definitionCases } from "../../harness";

definitionCases("Go to definition: interfaces", [
    {
        name: "an implements clause jumps to the interface",
        source: "interface Sized {\n  size: number;\n}\nclass Box implements __CURSOR__Sized {\n  size: number = 1;\n}\n",
        expectLines: [0],
    },
    {
        name: "an interface-typed parameter jumps to the interface",
        source: "interface Config {\n  retries: number;\n}\nfunction load(c: __CURSOR__Config): void {}\n",
        expectLines: [0],
    },
    {
        name: "an interface-typed const jumps to the interface",
        source: "interface Pair {\n  left: number;\n  right: number;\n}\nconst p: __CURSOR__Pair = { left: 1, right: 2 };\n",
        expectLines: [0],
    },
    {
        name: "an interface used across files jumps into the defining module",
        source: "import { Widget } from \"./util.cp3\";\nfunction render(w: __CURSOR__Widget): void {}\n",
        modules: [["util.cp3", "export interface Widget {\n  render(): void;\n}\n"]],
        expectLines: [0],
    },
    {
        name: "a second interface implementation still jumps to the same declaration",
        source: "interface Base {\n  run(): void;\n}\nclass A implements Base {\n  run(): void {}\n}\nclass B implements __CURSOR__Base {\n  run(): void {}\n}\n",
        expectLines: [0],
    },
    {
        name: "an interface member jumps to the member declaration",
        source: "interface Sized {\n  size: number;\n}\nfunction inspect(s: Sized): number {\n  return s.__CURSOR__size;\n}\n",
        expectLines: [1],
    },
    {
        name: "an interface extending another jumps to the parent interface",
        source: "interface Base {\n  id: number;\n}\ninterface Derived extends __CURSOR__Base {\n  extra: string;\n}\n",
        expectLines: [0],
    },
    {
        name: "an interface method in an implements list jumps to the member",
        source: "interface Runner {\n  run(): void;\n}\nclass Circle implements Runner {\n  run(): void {}\n}\nconst r = new Circle();\nr.__CURSOR__run();\n",
        expectLines: [1],
    },
    {
        name: "a member call resolves the implementing class member",
        source: "interface End {\n  halt(): void;\n}\nclass A implements End {\n  halt(): void {}\n}\nconst a = new A();\na.__CURSOR__halt();\n",
        expectLines: [4],
    },
    {
        name: "a default-exported interface jumps to its declaration from another file",
        source: "import Handler from \"./util.cp3\";\nfunction attach(h: __CURSOR__Handler): void {}\n",
        modules: [["util.cp3", "export default interface Handler {\n  fire(): void;\n}\n"]],
        expectLines: [0],
    },
]);
