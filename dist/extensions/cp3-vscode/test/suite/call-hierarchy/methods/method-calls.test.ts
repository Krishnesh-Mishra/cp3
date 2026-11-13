// Call hierarchy / method calls
import { callHierarchyCases } from "../../harness";

callHierarchyCases("Call hierarchy: methods", [
    {
        name: "the prepared hierarchy for a method names the method",
        source: "class Machine {\n  start(): void {}\n}\nconst m = new Machine();\nm.__CURSOR__start();\n",
        prepareContains: ["start"],
    },
    {
        name: "the outgoing call from a method names the method it invokes",
        source: "class Machine {\n  boot(): void {}\n  start(): void {\n    this.boot();\n  }\n}\nconst m = new Machine();\nm.__CURSOR__start();\n",
        prepareContains: ["start"],
        outgoingContains: ["boot"],
    },
    {
        name: "the incoming call from another method names the caller method",
        source: "class Machine {\n  boot(): void {}\n  start(): void {\n    this.boot();\n  }\n}\nconst m = new Machine();\nconst value = __CURSOR__m.boot();\n",
        prepareContains: ["boot"],
        incomingContains: ["start"],
    },
    {
        name: "a static method invoked on the class reports outgoing calls",
        source: "class Factory {\n  static make(): Factory {\n    return new Factory();\n  }\n}\nconst value = __CURSOR__Factory.make();\n",
        prepareContains: ["make"],
    },
    {
        name: "a class constructor hierarchy reports the constructor",
        source: "class Counter {\n  count: number = 0;\n  constructor() {}\n}\nconst c = new __CURSOR__Counter();\n",
        prepareContains: ["Counter"],
    },
    {
        name: "a method calling a module-level function reports the function as outgoing",
        source: "function helper(): number {\n  return 1;\n}\nclass Machine {\n  compute(): number {\n    return helper();\n  }\n}\nconst m = new Machine();\nconst value = __CURSOR__m.compute();\n",
        prepareContains: ["compute"],
        outgoingContains: ["helper"],
    },
    {
        name: "a method called from two callers reports both callers",
        source: "class Machine {\n  ping(): void {}\n  run(): void {\n    this.ping();\n  }\n  kick(): void {\n    this.ping();\n  }\n}\nconst m = new Machine();\nconst value = __CURSOR__m.ping();\n",
        prepareContains: ["ping"],
        incomingContains: ["run", "kick"],
    },
    {
        name: "an abstract method prepared from the base class reports the method",
        source: "abstract class Shape {\n  abstract area(): number;\n  describe(): number {\n    return 0;\n  }\n}\nclass Square extends Shape {\n  area(): number { return 1; }\n}\nconst sq = new Square();\nconst value = __CURSOR__sq.describe();\n",
        prepareContains: ["describe"],
    },
    {
        name: "a module-scope function called via a callback reports its callee",
        source: "function render(item: number): void {\n}\nfunction each(items: number[], cb: (item: number) => void): void {\n  cb(items[0]);\n}\neach([1, 2], render);\n",
        prepareContains: ["render"],
    },
    {
        name: "a getter invoked on an object reports the getter",
        source: "class Box {\n  get value(): number { return 1; }\n}\nconst b = new Box();\nconst value = b.__CURSOR__value;\n",
        prepareContains: ["value"],
    },
    {
        name: "a method that calls another method in a chain reports the inner one",
        source: "class Chain {\n  first(): number {\n    return 1;\n  }\n  second(): number {\n    return this.first() + 1;\n  }\n  third(): number {\n    return this.second() + 1;\n  }\n}\nconst c = new Chain();\nconst value = __CURSOR__c.third();\n",
        prepareContains: ["third"],
        outgoingContains: ["second"],
    },
    {
        name: "a private helper called from a public method reports the call",
        source: "class Strict {\n  private helper(): number {\n    return 1;\n  }\n  main(): number {\n    return this.helper();\n  }\n}\nconst s = new Strict();\nconst value = __CURSOR__s.main();\n",
        prepareContains: ["main"],
        outgoingContains: ["helper"],
    },
    {
        name: "a class static referenced in a type position still prepares",
        source: "class Machine {\n  static build(): void {}\n}\nMachine.__CURSOR__build();\n",
        prepareContains: ["build"],
    },
    {
        name: "a method called on a returned object reports the method",
        source: "class Factory {\n  make(): Machine {\n    return new Machine();\n  }\n}\nclass Machine {\n  start(): void {}\n}\nconst f = new Factory();\nf.make().__CURSOR__start();\n",
        prepareContains: ["start"],
    },
    {
        name: "an interface-typed method call reports the interface method",
        source: "interface Runner {\n  run(): void;\n}\nfunction go(r: Runner): void {\n  r.__CURSOR__run();\n}\nconst impl: Runner = { run(): void {} };\ngo(impl);\n",
        prepareContains: ["run"],
    },
]);
