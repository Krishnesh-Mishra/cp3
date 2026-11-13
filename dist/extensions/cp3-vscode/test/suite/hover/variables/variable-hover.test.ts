// Hover / variable types
import { hoverCases } from "../../harness";

hoverCases("Hover: variable types", [
    {
        name: "a const string hover reports the string type",
        source: "const s = \"x\";\nconst read = __CURSOR__s;\n",
        contains: ["string"],
    },
    {
        name: "a const number hover reports the number type",
        source: "const n = 1 + 2;\nconst read = __CURSOR__n;\n",
        contains: ["number"],
    },
    {
        name: "a const boolean hover reports the boolean type",
        source: "const b = true;\nconst read = __CURSOR__b;\n",
        contains: ["boolean"],
    },
    {
        name: "an inferred array hover reports the element type",
        source: "const xs = [1, 2, 3];\nconst read = __CURSOR__xs;\n",
        contains: ["number"],
    },
    {
        name: "an explicitly typed const shows the declared type",
        source: "const value: string = \"hi\";\nconst read = __CURSOR__value;\n",
        contains: ["string"],
    },
    {
        name: "a typed parameter hover reports its annotated type",
        source: "function f(value: string): string {\n  return __CURSOR__value;\n}\n",
        contains: ["string"],
    },
    {
        name: "a number parameter hover reports number",
        source: "function f(amount: number): number {\n  return __CURSOR__amount;\n}\n",
        contains: ["number"],
    },
    {
        name: "a boolean parameter hover reports boolean",
        source: "function f(active: boolean): void {\n  const copy = __CURSOR__active;\n}\n",
        contains: ["boolean"],
    },
    {
        name: "a hover of the result of a string method reports the string type",
        source: "const text = \"hello\";\nconst up = text.toUpperCase();\nconst read = __CURSOR__up;\n",
        contains: ["string"],
    },
    {
        name: "a hover of a variable used in a member call reports its type",
        source: "const text = \"hello\";\nconst up = __CURSOR__text.toUpperCase();\n",
        contains: ["string"],
    },
]);
