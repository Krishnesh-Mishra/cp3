// Type hierarchy / interfaces and implementations
import { typeHierarchyCases } from "../../harness";

typeHierarchyCases("Type hierarchy: interfaces", [
    {
        name: "the prepared hierarchy of an interface names the interface",
        source: "interface Sized {\n  size(): number;\n}\nfunction inspect(s: __CURSOR__Sized): void {}\n",
        prepareContains: ["Sized"],
    },
    {
        name: "an interface extending another has the parent as supertype",
        source: "interface Base {\n  id: number;\n}\ninterface Derived extends __CURSOR__Base {\n  extra: string;\n}\n",
        prepareContains: ["Base"],
    },
    {
        name: "an interface supertype of the extending interface",
        source: "interface Base {\n  id: number;\n}\ninterface Derived extends Base {\n  extra: string;\n}\nconst impl = __CURSOR__new class implements Derived {\n  id: number = 1;\n  extra: string = \"e\";\n};\n",
        prepareContains: ["Derived"],
        supertypesContains: ["Base"],
    },
    {
        name: "a class implementing an interface shows the interface as supertype",
        source: "interface Sized {\n  size(): number;\n}\nclass Impl implements Sized {\n  size(): number { return 1; }\n}\nconst impl = new __CURSOR__Impl();\n",
        prepareContains: ["Impl"],
        supertypesContains: ["Sized"],
    },
    {
        name: "a class implementing two interfaces shows both as supertypes",
        source: "interface Alpha {\n  alpha(): void;\n}\ninterface Beta {\n  beta(): void;\n}\nclass Impl implements Alpha, Beta {\n  alpha(): void {}\n  beta(): void {}\n}\nconst impl = new __CURSOR__Impl();\n",
        prepareContains: ["Impl"],
        supertypesContains: ["Alpha", "Beta"],
    },
    {
        name: "subtypes of an interface include the implementing class",
        source: "interface Sized {\n  size(): number;\n}\nclass Impl implements Sized {\n  size(): number { return 1; }\n}\ninterface SizedRef extends Sized {}\nconst ref: __CURSOR__Sized = new Impl();\n",
        prepareContains: ["Sized"],
        subtypesContains: ["Impl"],
    },
    {
        name: "an interface with a default implementation shows the class as subtype",
        source: "interface Named {\n  name: string;\n}\nclass Cat implements Named {\n  name: string = \"miaow\";\n}\nconst cat = new Cat();\nconst named: __CURSOR__Named = cat;\n",
        prepareContains: ["Named"],
        subtypesContains: ["Cat"],
    },
    {
        name: "a class hierarchy with a shared interface keeps the class subtypes",
        source: "interface Sized {\n  size(): number;\n}\nclass Base implements Sized {\n  size(): number { return 1; }\n}\nclass Derived extends Base {}\nconst base = new __CURSOR__Base();\n",
        prepareContains: ["Base"],
        subtypesContains: ["Derived"],
    },
    {
        name: "an interface extending a generic base keeps the parent supertype",
        source: "interface Pair<A, B> {\n  first: A;\n}\ninterface NumberPair extends __CURSOR__Pair<number, string> {\n  second: string;\n}\n",
        prepareContains: ["Pair"],
    },
    {
        name: "a class hierarchy under an interface shows all concrete implementations",
        source: "interface Shape {\n  area(): number;\n}\nclass Square implements Shape {\n  area(): number { return 1; }\n}\nclass Circle implements Shape {\n  area(): number { return 2; }\n}\nconst shape: __CURSOR__Shape = new Square();\n",
        prepareContains: ["Shape"],
        subtypesContains: ["Square", "Circle"],
    },
    {
        name: "a subclass-vs-interface supertype relationship is reachable",
        source: "interface Sized {\n  size(): number;\n}\nclass Base implements Sized {\n  size(): number { return 1; }\n}\nclass Derived extends Base {}\nconst derived = new __CURSOR__Derived();\n",
        prepareContains: ["Derived"],
        supertypesContains: ["Base"],
    },
    {
        name: "an interface implementation list matches the interface name",
        source: "interface Runner {\n  run(): void;\n}\nclass Circle implements __CURSOR__Runner {\n  run(): void {}\n}\n",
        prepareContains: ["Runner"],
    },
    {
        name: "a second interface in a multi-implements line is found as supertype",
        source: "interface Alpha {\n  alpha(): void;\n}\ninterface Beta {\n  beta(): void;\n}\nclass Impl implements Alpha, Beta {\n  alpha(): void {}\n  beta(): void {}\n}\nconst impl = new __CURSOR__Impl();\n",
        prepareContains: ["Impl"],
        supertypesContains: ["Beta"],
    },
    {
        name: "a qualified interface supertype keeps its own name",
        source: "interface Base {\n  id: number;\n}\ninterface Mid extends Base {\n  extra: string;\n}\ninterface Leaf extends Mid {\n  more: boolean;\n}\nconst leaf: __CURSOR__Leaf = { id: 1, extra: \"e\", more: true };\n",
        prepareContains: ["Leaf"],
        supertypesContains: ["Mid"],
    },
    {
        name: "a class at the top of a deep interface chain keeps the nearest parent",
        source: "interface A {\n  a(): void;\n}\ninterface B extends A {\n  b(): void;\n}\ninterface C extends B {\n  c(): void;\n}\nclass Impl implements C {\n  a(): void {}\n  b(): void {}\n  c(): void {}\n}\nconst impl = new __CURSOR__Impl();\n",
        prepareContains: ["Impl"],
        supertypesContains: ["C"],
    },
]);
