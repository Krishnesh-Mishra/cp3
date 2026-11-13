// Autocomplete / completion ranking
import { completionCases } from "../../harness";

completionCases("Autocomplete: completion ranking", [
    {
        name: "local declared above leads the popup over globals",
        source: "function main(): number {\n  const rankTarget = 10;\n  return __CURSOR__;\n}\n",
        expectPresent: ["rankTarget"],
        timeoutMs: 30_000,
    },
    {
        name: "nearest scope leads the popup",
        source: "function main(): number {\n  const outerName = 1;\n  if (true) {\n    const innerName = 2;\n    return __CURSOR__;\n  }\n  return outerName;\n}\n",
        expectPresent: ["innerName", "outerName"],
    },
    {
        name: "parameter leads over a function with the same prefix",
        source: "function join(item: string): string {\n  return __CURSOR__;\n}\n",
        expectPresent: ["item"],
    },
    {
        name: "recently declared name appears alongside older names",
        source: "function main(): number {\n  const staleValue = 1;\n  const freshValue = 2;\n  return __CURSOR__;\n}\n",
        expectPresent: ["staleValue", "freshValue"],
    },
    {
        name: "imported member is offered beside locals with same prefix",
        source: "import Math from \"std/math\";\nconst floorCount = 0;\nconst value = __CURSOR__;\n",
        expectPresent: ["floorCount"],
    },
]);
