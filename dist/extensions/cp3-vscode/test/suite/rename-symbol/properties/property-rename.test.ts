// Rename symbol / properties and interfaces
import { renameCases } from "../../harness";

renameCases("Rename symbol: properties and interfaces", [
    {
        name: "an object property is renamed at the declaration and reads",
        source: "const user = { name: \"Test\" };\nconst read = user.__CURSOR__name;\n",
        newName: "owner",
    },
    {
        name: "a class property is renamed at the field and an external read",
        source: "class Machine {\n  speed: number = 0;\n}\nconst m = new Machine();\nconst read = m.__CURSOR__speed;\n",
        newName: "velocity",
        expectEditCount: 2,
    },
    {
        name: "a class property renamed twice renames at both use sites",
        source: "class Machine {\n  count: number = 0;\n}\nconst m = new Machine();\nm.__CURSOR__count = 5;\nconst read = m.count;\n",
        newName: "total",
        expectEditCount: 3,
    },
    {
        name: "an interface property is renamed at the interface and the implementation",
        source: "interface Sized {\n  __CURSOR__length: number;\n}\nclass Impl implements Sized {\n  length: number = 1;\n}\n",
        newName: "capacity",
    },
    {
        name: "an interface is renamed across declarations and usages",
        source: "interface Sized {\n  size: number;\n}\nfunction f(s: __CURSOR__Sized): number {\n  return s.size;\n}\n",
        newName: "Measurable",
        expectEditCount: 2,
    },
    {
        name: "an enum member is renamed at the declaration and use site",
        source: "enum Mode {\n  Fast,\n  Slow,\n}\nconst current = __CURSOR__Mode.Fast;\n",
        newName: "Quick",
    },
    {
        name: "a type alias is renamed at declaration and annotation",
        source: "type Radians = number;\nconst value: __CURSOR__Radians = 1;\n",
        newName: "Angle",
        expectEditCount: 2,
    },
    {
        name: "a getter is renamed at the declaration and the read",
        source: "class Box {\n  get __CURSOR__value(): number { return 1; }\n}\nconst b = new Box();\nconst read = b.value;\n",
        newName: "contents",
    },
    {
        name: "an interface renamed in two implements clauses renames both",
        source: "interface Sized {\n  size: number;\n}\nclass A implements Sized {\n  size: number = 1;\n}\nclass B implements __CURSOR__Sized {\n  size: number = 2;\n}\n",
        newName: "Measured",
        expectEditCount: 3,
    },
    {
        name: "a property on an object literal is renamed at all read sites",
        source: "const config = { retries: 3, timeout: 100 };\nconst a = config.__CURSOR__retries;\nconst b = config.retries;\n",
        newName: "attempts",
        expectEditCount: 3,
    },
]);
