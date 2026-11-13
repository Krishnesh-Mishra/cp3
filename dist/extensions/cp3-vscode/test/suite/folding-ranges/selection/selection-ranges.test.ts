// Selection ranges / expand selection nests
import { selectionRangeCases } from "../../harness";

selectionRangeCases("Selection ranges: nesting", [
    {
        name: "selection inside a statement nests the expression tokens",
        source: "const value = __CURSOR__total + 1;\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a function argument nests the argument",
        source: "function bump(n: number): void {}\nbump(__CURSOR__value);\n",
        expectRanges: 2,
    },
    {
        name: "selection inside an object property nests the property",
        source: "const config = { retries: __CURSOR__3 };\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a call expression nests the call",
        source: "function compute(): number { return 1; }\nconst out = __CURSOR__compute();\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a template interpolation nests the expression",
        source: "const n = 3;\nconst label = `value: ${__CURSOR__n}`;\n",
        expectRanges: 2,
    },
    {
        name: "selection inside an array element nests the element",
        source: "const xs = [1, __CURSOR__2, 3];\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a return statement nests the expression",
        source: "function f(): number {\n  return __CURSOR__1 + 2;\n}\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a binary expression nests the operands",
        source: "const n = 1 + 2;\nconst sum = __CURSOR__n * 2;\n",
        expectRanges: 2,
    },
    {
        name: "selection inside an object property value nests the member",
        source: "const user = { name: \"Test\" };\nconst read = user.__CURSOR__name;\n",
        expectRanges: 2,
    },
    {
        name: "selection inside a class property initializer nests the value",
        source: "class Counter {\n  count: number = __CURSOR__0;\n}\n",
        expectRanges: 2,
    },
]);
