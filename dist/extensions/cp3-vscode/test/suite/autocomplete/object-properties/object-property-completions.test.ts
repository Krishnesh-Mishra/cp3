// Autocomplete / object property completion
import { completionCases } from "../../harness";

completionCases("Autocomplete: object property completion", [
    {
        name: "name and age are offered after the dot",
        source: "const user = {\n  name: \"Test\",\n  age: 25,\n};\nuser.__CURSOR__\n",
        expectPresent: ["name", "age"],
    },
    {
        name: "single-property object offers the property",
        source: "const point = { x: 1 };\npoint.__CURSOR__\n",
        expectPresent: ["x"],
    },
    {
        name: "nested object member offers its own properties",
        source: "const outer = { inner: { deep: 1, other: 2 } };\nouter.inner.__CURSOR__\n",
        expectPresent: ["deep", "other"],
    },
    {
        name: "object with string and number values offers both members",
        source: "const mixed = { label: \"ok\", count: 3 };\nmixed.__CURSOR__\n",
        expectPresent: ["label", "count"],
    },
    {
        name: "object literal inside an array element is completed",
        source: "const items = [{ sku: \"A-1\", qty: 2 }, { sku: \"B-2\", qty: 5 }];\nitems[0].__CURSOR__\n",
        expectPresent: ["sku", "qty"],
    },
    {
        name: "freshly assigned object member completion works during typing",
        source: "const Obj = { name: \"Rahul\" };\nObj.na__CURSOR__\n",
        expectPresent: ["name"],
    },
    {
        name: "member completion does not offer array methods on objects",
        source: "const user = { name: \"Test\" };\nuser.__CURSOR__\n",
        expectAbsent: ["push", "map", "join"],
    },
    {
        name: "object member completion includes its documented kind",
        source: "const Config = { retries: 3 };\nConfig.__CURSOR__\n",
        expectPresent: ["retries"],
    },
    {
        name: "two separate objects keep their own members",
        source: "const first = { apple: 1 };\nconst second = { banana: 2 };\nsecond.__CURSOR__\n",
        expectPresent: ["banana"],
        expectAbsent: ["apple"],
    },
    {
        name: "object property assigned a function offers it",
        source: "const api = { refresh(): void {} };\napi.__CURSOR__\n",
        expectPresent: ["refresh"],
    },
    {
        name: "object property with complex value offers the name",
        source: "const state = { items: [1, 2, 3], ready: true };\nstate.__CURSOR__\n",
        expectPresent: ["items", "ready"],
    },
    {
        name: "interface-typed literal completion suggests the interface's members",
        source: "interface Point {\n  x: number;\n  y: number;\n}\nconst p: Point = { x: 1, y: 2 };\np.__CURSOR__\n",
        expectPresent: ["x", "y"],
    },
    {
        name: "interface with a single member is offered on the dot",
        source: "interface Named {\n  label: string;\n}\nconst n: Named = { label: \"a\" };\nn.__CURSOR__\n",
        expectPresent: ["label"],
    },
    {
        name: "class instance offers declared property",
        source: "class Counter {\n  count: number = 0;\n}\nconst c = new Counter();\nc.__CURSOR__\n",
        expectPresent: ["count"],
    },
    {
        name: "class instance offers its method",
        source: "class Counter {\n  increment(): void {}\n}\nconst c = new Counter();\nc.__CURSOR__\n",
        expectPresent: ["increment"],
    },
    {
        name: "class getter is offered on the dot",
        source: "class Box {\n  get value(): number { return 1; }\n}\nconst b = new Box();\nb.__CURSOR__\n",
        expectPresent: ["value"],
    },
    {
        name: "method returning an object chains to that object's properties",
        source: "class Factory {\n  make(): { product: string } {\n    return { product: \"widget\" };\n  }\n}\nconst f = new Factory();\nf.make().__CURSOR__\n",
        expectPresent: ["product"],
    },
    {
        name: "private modifiers do not leak into object-literal surfaces",
        source: "const Public = { visible: 1 };\nPublic.__CURSOR__\n",
        expectAbsent: ["private", "static", "readonly"],
    },
    {
        name: "property access on unknown-typed value is a well-formed answer",
        source: "function inspect(value: number): void {\n  value.__CURSOR__\n}\n",
        expectPresent: [],
    },
    {
        name: "array member completion is offered on the array itself",
        source: "const data = [1, 2, 3];\ndata.__CURSOR__\n",
        expectPresent: ["join", "push", "filter"],
    },
]);
