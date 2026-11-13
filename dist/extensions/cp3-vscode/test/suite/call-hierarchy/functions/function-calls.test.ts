// Call hierarchy / incoming and outgoing calls
import { callHierarchyCases } from "../../harness";

callHierarchyCases("Call hierarchy: functions", [
    {
        name: "the prepared call hierarchy for a function names the function",
        source: "function helper(): number {\n  return 1;\n}\nconst value = __CURSOR__helper();\n",
        prepareContains: ["helper"],
    },
    {
        name: "the outgoing call from a function names the callee",
        source: "function inner(): number {\n  return 1;\n}\nfunction outer(): number {\n  return inner();\n}\nconst value = __CURSOR__outer();\n",
        prepareContains: ["outer"],
        outgoingContains: ["inner"],
    },
    {
        name: "the outgoing call from a function names the std member it calls",
        source: "import console from \"std/console\";\nfunction outer(): void {\n  console.log(\"hi\");\n}\nconst value = __CURSOR__outer();\n",
        prepareContains: ["outer"],
        outgoingContains: ["log"],
    },
    {
        name: "an empty function makes no outgoing calls",
        source: "function empty(): void {\n}\nconst value = __CURSOR__empty();\n",
        prepareContains: ["empty"],
        outgoingContains: [],
    },
    {
        name: "a function calling another names both the outer and inner",
        source: "function inner(): number {\n  return 2;\n}\nfunction outer(): number {\n  return inner() + inner();\n}\nconst first = __CURSOR__outer();\n",
        prepareContains: ["outer"],
        outgoingContains: ["inner"],
    },
    {
        name: "the incoming call from the caller body names the caller",
        source: "function helper(): number {\n  return 1;\n}\nfunction caller(): number {\n  return helper();\n}\nconst value = __CURSOR__helper();\n",
        prepareContains: ["helper"],
        incomingContains: ["caller"],
    },
    {
        name: "a function called from two call sites reports both callers",
        source: "function shared(): number {\n  return 1;\n}\nfunction callerOne(): number {\n  return shared();\n}\nfunction callerTwo(): number {\n  return shared();\n}\nconst value = __CURSOR__shared();\n",
        prepareContains: ["shared"],
        incomingContains: ["callerOne", "callerTwo"],
    },
    {
        name: "a recursive function reports itself as an incoming call",
        source: "function rec(n: number): number {\n  if (n <= 0) { return 0; }\n  return rec(n - 1);\n}\nconst value = __CURSOR__rec(3);\n",
        prepareContains: ["rec"],
        incomingContains: ["rec"],
    },
    {
        name: "a deeply nested call chain reports the immediate callee",
        source: "function deep(): number {\n  return 1;\n}\nfunction mid(): number {\n  return deep();\n}\nfunction top(): number {\n  return mid();\n}\nconst value = __CURSOR__top();\n",
        prepareContains: ["top"],
        outgoingContains: ["mid"],
    },
    {
        name: "a callback invocation reports the callee it passes",
        source: "function render(item: number): void {\n}\nfunction each(items: number[], cb: (item: number) => void): void {\n  for (const item of items) {\n    cb(item);\n  }\n}\neach([1, 2], render);\n",
        prepareContains: ["render"],
    },
]);
