// Integration / classes and inheritance
import { completionCases, hoverCases, referencesCases } from "../../harness";

completionCases("Integration: classes + inheritance (completion)", [
    { name: "subclass instance offers inherited member", source: "class Base { tag: string = \"b\"; }\nclass Derived extends Base {}\nconst d = new Derived();\nd.__CURSOR__\n", expectPresent: ["tag"] },
    { name: "subclass offers base method on the dot", source: "class Base { greet(): void {} }\nclass Derived extends Base {}\nconst d = new Derived();\nd.__CURSOR__\n", expectPresent: ["greet"] },
    { name: "concrete subclass of abstract class offers abstract member implementation", source: "abstract class Shape { abstract area(): number; }\nclass Square extends Shape { area(): number { return 1; } }\nconst s = new Square();\ns.__CURSOR__\n", expectPresent: ["area"] },
    { name: "a grandchild instance offers a grandparent member", source: "class A { a(): void {} }\nclass B extends A {}\nclass C extends B {}\nconst c = new C();\nc.__CURSOR__\n", expectPresent: ["a"] },
    { name: "a sibling subclass does not inherit the other sibling's members", source: "class Base {}\nclass Left extends Base { leftOnly(): void {} }\nclass Right extends Base {}\nconst r = new Right();\nr.__CURSOR__\n", expectAbsent: ["leftOnly"] },
    { name: "an instance of an implementing class offers interface members", source: "interface Sized { size(): number }\nclass Impl implements Sized { size(): number { return 1; } }\nconst i = new Impl();\ni.__CURSOR__\n", expectPresent: ["size"] },
    { name: "a class implementing two interfaces offers both members", source: "interface A { a(): void }\ninterface B { b(): void }\nclass Impl implements A, B { a(): void {} b(): void {} }\nconst i = new Impl();\ni.__CURSOR__\n", expectPresent: ["a", "b"] },
    { name: "a static member is offered on the derived class", source: "class Base { static who(): string { return \"b\"; } }\nclass Derived extends Base {}\nDerived.__CURSOR__\n", expectPresent: ["who"] },
    { name: "an overridden member is offered once without duplication", source: "class Base { level: number = 0; }\nclass Mid extends Base { level: number = 1; }\nconst m = new Mid();\nm.__CURSOR__\n", expectPresent: ["level"] },
    { name: "an instance of a generic subclass offers inherited members", source: "class Box<T> { value: T = 0 as T; }\nclass StringBox extends Box<string> {}\nconst b = new StringBox();\nb.__CURSOR__\n", expectPresent: ["value"] },
]);

hoverCases("Integration: classes + inheritance (hover)", [
    { name: "subclass hover on inherited member shows the base signature", source: "class Base { greet(): void {} }\nclass Derived extends Base {}\nconst d = new Derived();\nd.__CURSOR__greet();\n", contains: ["greet"] },
    { name: "inherited static hover shows the declaring class", source: "class Base { static build(): void {} }\nclass Derived extends Base {}\nDerived.__CURSOR__build();\n", contains: ["build"] },
    { name: "an overridden member hover shows the overriding signature", source: "class Base { size(): number { return 1; } }\nclass Derived extends Base { size(): number { return 2; } }\nconst d = new Derived();\nd.__CURSOR__size();\n", contains: ["size"] },
    { name: "an abstract method hover shows the abstract signature", source: "abstract class Shape { abstract area(): number; }\nclass Square extends Shape { area(): number { return 1; } }\nconst s = new Square();\ns.__CURSOR__area();\n", contains: ["area"] },
    { name: "a concrete override hover reports the override's type", source: "class Base { count(): number { return 1; } }\nclass Derived extends Base { count(): number { return 2; } }\nconst d = new Derived();\nd.__CURSOR__count();\n", contains: ["number"] },
    { name: "a base class hover reports the base class name", source: "class Base {}\nclass Derived extends __CURSOR__Base {}\n", contains: ["Base"] },
    { name: "a getter override hover reports the getter", source: "class Base { get value(): number { return 0; } }\nclass Derived extends Base {}\nconst d = new Derived();\nconst read = d.__CURSOR__value;\n", contains: ["value"] },
    { name: "an interface-implementing class hover reports the interface method", source: "interface Sized { size(): number }\nclass Impl implements Sized { size(): number { return 1; } }\nconst i = new Impl();\ni.__CURSOR__size();\n", contains: ["size"] },
    { name: "a chained base member hover reports the member", source: "class A { inner: { deep(): void } = { deep(): void {} }; }\nconst a = new A();\na.inner.__CURSOR__deep();\n", contains: ["deep"] },
    { name: "a generic base member hover reports the parameter", source: "class Base<T> { value: T = 0 as T; }\nclass Derived extends Base<number> {}\nconst d = new Derived();\nconst read = d.__CURSOR__value;\n", contains: ["value"] },
]);

referencesCases("Integration: classes + inheritance (references)", [
    { name: "base class referenced in a subclass extends is found at both sites", source: "class Base {}\nclass Derived extends __CURSOR__Base {}\nconst b = new Base();\n", expectSomeLines: [0, 1], includeDeclaration: true },
    { name: "base class referenced in type position is found", source: "class Base {}\nclass Derived extends Base {}\nconst shapes: __CURSOR__Base[] = [new Derived()];\n", expectSomeLines: [0, 2], includeDeclaration: true },
    { name: "base method call on a subclass instance is found at both sites", source: "class Base { greet(): void {} }\nclass Derived extends Base {}\nconst a = new Base();\nconst b = new Derived();\na.greet();\nb.__CURSOR__greet();\n", expectSomeLines: [1, 4, 5], includeDeclaration: true },
    { name: "interface declaration is found at the implements clause", source: "interface Sized {\n  size(): number;\n}\nclass Box implements __CURSOR__Sized {\n  size(): number { return 1; }\n}\n", expectSomeLines: [0, 3], includeDeclaration: true },
    { name: "an interface member is found across two implementers", source: "interface Sized {\n  size(): number;\n}\nclass A implements Sized { size(): number { return 1; } }\nclass B implements Sized { size(): number { return 2; } }\nfunction f(s: __CURSOR__Sized): number { return s.size(); }\n", expectSomeLines: [0, 3, 4, 5], includeDeclaration: true },
    { name: "an abstract member is found across subclasses", source: "abstract class Shape { abstract area(): number; }\nclass Square extends Shape { area(): number { return 1; } }\nclass Circle extends Shape { area(): number { return 2; } }\nfunction f(s: __CURSOR__Shape): number { return s.area(); }\n", expectSomeLines: [0, 5], includeDeclaration: true },
    { name: "a property of a subclass is found at declaration and reads", source: "class Base { tag: string = \"b\"; }\nclass Derived extends Base {}\nconst d = new Derived();\nconst tag = d.__CURSOR__tag;\nconst again = d.tag;\n", expectSomeLines: [0, 3, 4], includeDeclaration: true },
    { name: "an interface alias referenced from two files is found", source: "import { Sized } from \"./util.cp3\";\nfunction f(s: Sized): void {}\nclass Impl implements Sized { size(): number { return 1; } }\n", modules: [["util.cp3", "export interface Sized {\n  size(): number;\n}\n"]], expectSomeLines: [0, 1, 2], includeDeclaration: true },
    { name: "a static member references are found on both class accesses", source: "class Base { static who(): void {} }\nclass Derived extends Base {}\nBase.__CURSOR__who();\nDerived.who();\n", expectSomeLines: [0, 2, 3], includeDeclaration: true },
    { name: "a type alias used by multiple class annotations is found", source: "type Radians = number;\nclass A { angle: __CURSOR__Radians = 1; }\nclass B { angle: Radians = 2; }\n", expectSomeLines: [0, 1, 2], includeDeclaration: true },
]);
