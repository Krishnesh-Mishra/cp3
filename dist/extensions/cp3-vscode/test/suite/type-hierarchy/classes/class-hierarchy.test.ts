// Type hierarchy / classes and inheritance
import { typeHierarchyCases } from "../../harness";

typeHierarchyCases("Type hierarchy: class inheritance", [
    {
        name: "the prepared hierarchy for a leaf class names the leaf",
        source: "class Base {}\nclass Mid extends Base {}\nclass Leaf extends Mid {}\nconst leaf = new __CURSOR__Leaf();\n",
        prepareContains: ["Leaf"],
    },
    {
        name: "the supertype of the leaf is its immediate parent",
        source: "class Base {}\nclass Mid extends Base {}\nclass Leaf extends Mid {}\nconst leaf = new __CURSOR__Leaf();\n",
        prepareContains: ["Leaf"],
        supertypesContains: ["Mid"],
    },
    {
        name: "the supertype of the middle class is the base",
        source: "class Base {}\nclass Mid extends Base {}\nconst mid = new __CURSOR__Mid();\n",
        prepareContains: ["Mid"],
        supertypesContains: ["Base"],
    },
    {
        name: "the hierarchy is prepared on the class identifier",
        source: "class Base {}\nclass Mid extends Base {}\nconst base = new __CURSOR__Base();\n",
        prepareContains: ["Base"],
    },
    {
        name: "a three-level hierarchy supertype of the middle is the base",
        source: "class Base {}\nclass Mid extends Base {}\nclass Leaf extends Mid {}\nconst mid = new __CURSOR__Mid();\n",
        prepareContains: ["Mid"],
        supertypesContains: ["Base"],
    },
    {
        name: "subtypes of the base class include the middle class",
        source: "class Base {}\nclass Mid extends Base {}\nconst base = new __CURSOR__Base();\n",
        prepareContains: ["Base"],
        subtypesContains: ["Mid"],
    },
    {
        name: "subtypes of the middle class include the leaf class",
        source: "class Base {}\nclass Mid extends Base {}\nclass Leaf extends Mid {}\nconst mid = new __CURSOR__Mid();\n",
        prepareContains: ["Mid"],
        subtypesContains: ["Leaf"],
    },
    {
        name: "a two-branch hierarchy names both subtypes",
        source: "class Base {}\nclass Left extends Base {}\nclass Right extends Base {}\nconst base = new __CURSOR__Base();\n",
        prepareContains: ["Base"],
        subtypesContains: ["Left", "Right"],
    },
    {
        name: "an abstract base class shows its concrete subclasses",
        source: "abstract class Shape {\n  abstract area(): number;\n}\nclass Square extends Shape {\n  area(): number { return 1; }\n}\nclass Circle extends Shape {\n  area(): number { return 2; }\n}\nconst shape = new __CURSOR__Shape();\n",
        prepareContains: ["Shape"],
        subtypesContains: ["Square", "Circle"],
    },
    {
        name: "a wide hierarchy's supertype chain includes both parents",
        source: "class Animal {}\nclass Mammal extends Animal {}\nclass Dog extends Mammal {}\nconst dog = new __CURSOR__Dog();\n",
        prepareContains: ["Dog"],
        supertypesContains: ["Mammal"],
    },
]);
