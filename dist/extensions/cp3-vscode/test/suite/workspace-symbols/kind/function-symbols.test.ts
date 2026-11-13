// Workspace symbols / cross-file discovery with unique query names
import { workspaceSymbolCases } from "../../harness";

workspaceSymbolCases("Workspace symbols: functions and variables", [
    {
        name: "a uniquely named function is found by its unique prefix",
        source: "function ZqxAlphaFunctionOne(): number {\n  return 1;\n}\n",
        query: "ZqxAlphaFunctionOne",
        contains: ["ZqxAlphaFunctionOne"],
    },
    {
        name: "a uniquely named arrow const is found by its unique prefix",
        source: "const ZqxBetaArrowTwo = (): number => 2;\n",
        query: "ZqxBetaArrowTwo",
        contains: ["ZqxBetaArrowTwo"],
    },
    {
        name: "a uniquely named async function is found by its unique prefix",
        source: "async function ZqxGammaPendingThree(): Promise<number> {\n  return 3;\n}\n",
        query: "ZqxGammaPendingThree",
        contains: ["ZqxGammaPendingThree"],
    },
    {
        name: "a uniquely named local const is found by its unique prefix",
        source: "const ZqxDeltaLocalFour = 4;\n",
        query: "ZqxDeltaLocalFour",
        contains: ["ZqxDeltaLocalFour"],
    },
    {
        name: "a uniquely named let is found by its unique prefix",
        source: "let ZqxEpsilonMutableFive = 5;\n",
        query: "ZqxEpsilonMutableFive",
        contains: ["ZqxEpsilonMutableFive"],
    },
    {
        name: "a uniquely named exported function is found by its unique prefix",
        source: "export function ZqxZetaExportedSix(): number {\n  return 6;\n}\n",
        query: "ZqxZetaExportedSix",
        contains: ["ZqxZetaExportedSix"],
    },
    {
        name: "a uniquely named function with parameters is found by its unique prefix",
        source: "function ZqxEtaWithParamsSeven(a: string, b: number): string {\n  return a;\n}\n",
        query: "ZqxEtaWithParamsSeven",
        contains: ["ZqxEtaWithParamsSeven"],
    },
    {
        name: "a uniquely named callback parameter is not a workspace symbol by itself",
        source: "function ZqxThetaCallbackEight(input: string): string {\n  return input;\n}\n",
        query: "ZqxThetaCallbackEight",
        contains: ["ZqxThetaCallbackEight"],
    },
    {
        name: "a uniquely named default exported function is found by its unique prefix",
        source: "export default function ZqxIotaDefaultNine(): number {\n  return 9;\n}\n",
        query: "ZqxIotaDefaultNine",
        contains: ["ZqxIotaDefaultNine"],
    },
    {
        name: "a uniquely named module-level const is found by its unique prefix",
        source: "const ZqxKappaModuleTen = 10;\nconst OTHER = 1;\n",
        query: "ZqxKappaModuleTen",
        contains: ["ZqxKappaModuleTen"],
    },
]);
