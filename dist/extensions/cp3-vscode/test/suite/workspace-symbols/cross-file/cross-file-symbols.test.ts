// Workspace symbols / cross-file discovery across multiple files
import { workspaceSymbolCases } from "../../harness";

workspaceSymbolCases("Workspace symbols: cross-file", [
    {
        name: "a uniquely named export in another module is found",
        source: "import { ZqxPhiModuleSymbolOne } from \"./lib.cp3\";\nconst value = ZqxPhiModuleSymbolOne();\n",
        modules: [["lib.cp3", "export function ZqxPhiModuleSymbolOne(): number { return 1; }\n"]],
        query: "ZqxPhiModuleSymbolOne",
        contains: ["ZqxPhiModuleSymbolOne"],
    },
    {
        name: "a uniquely named interface export in another module is found",
        source: "import { ZqxChiModuleIfaceTwo } from \"./lib.cp3\";\nfunction f(s: ZqxChiModuleIfaceTwo): void {}\n",
        modules: [["lib.cp3", "export interface ZqxChiModuleIfaceTwo {\n  size(): number;\n}\n"]],
        query: "ZqxChiModuleIfaceTwo",
        contains: ["ZqxChiModuleIfaceTwo"],
    },
    {
        name: "a uniquely named default export in another module is found",
        source: "import ZqxPsiDefaultModuleThree from \"./lib.cp3\";\nconst value = ZqxPsiDefaultModuleThree();\n",
        modules: [["lib.cp3", "export default function ZqxPsiDefaultModuleThree(): number { return 3; }\n"]],
        query: "ZqxPsiDefaultModuleThree",
        contains: ["ZqxPsiDefaultModuleThree"],
    },
    {
        name: "a uniquely named barrel-re-exported function is found",
        source: "import { ZqxOmegaBarrelFour } from \"./barrel.cp3\";\nconst value = ZqxOmegaBarrelFour();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export function ZqxOmegaBarrelFour(): number { return 4; }\n"]],
        query: "ZqxOmegaBarrelFour",
        contains: ["ZqxOmegaBarrelFour"],
    },
    {
        name: "a uniquely named barrel-re-exported class is found",
        source: "import { ZqxAlphaBarrelClassFive } from \"./barrel.cp3\";\nconst value = new ZqxAlphaBarrelClassFive();\n",
        modules: [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", "export class ZqxAlphaBarrelClassFive {\n  render(): void {}\n}\n"]],
        query: "ZqxAlphaBarrelClassFive",
        contains: ["ZqxAlphaBarrelClassFive"],
    },
    {
        name: "a uniquely named export with a path in a nested folder is found",
        source: "import { ZqxBetaNestedSix } from \"./lib/nested/deep.cp3\";\nconst value = ZqxBetaNestedSix();\n",
        modules: [["lib/nested/deep.cp3", "export function ZqxBetaNestedSix(): number { return 6; }\n"]],
        query: "ZqxBetaNestedSix",
        contains: ["ZqxBetaNestedSix"],
    },
    {
        name: "a uniquely named aliased re-export is found under its own name",
        source: "import { ZqxGammaAliasSeven } from \"./barrel.cp3\";\nconst value = ZqxGammaAliasSeven();\n",
        modules: [["barrel.cp3", "export { ZqxGammaOriginalSeven as ZqxGammaAliasSeven } from \"./base.cp3\";\n"], ["base.cp3", "export function ZqxGammaOriginalSeven(): number { return 7; }\n"]],
        query: "ZqxGammaAliasSeven",
        contains: ["ZqxGammaAliasSeven"],
    },
    {
        name: "a uniquely named local const is found even from outside its file",
        source: "const ZqxDeltaLocalEight = 8;\n",
        modules: [["other.cp3", "export const UNRELATED: number = 1;\n"]],
        query: "ZqxDeltaLocalEight",
        contains: ["ZqxDeltaLocalEight"],
    },
    {
        name: "a uniquely named const used across files is found once by the provider",
        source: "import { ZqxEpsilonSharedNine } from \"./lib.cp3\";\nconst read = ZqxEpsilonSharedNine;\n",
        modules: [["lib.cp3", "export const ZqxEpsilonSharedNine: number = 9;\n"]],
        query: "ZqxEpsilonSharedNine",
        contains: ["ZqxEpsilonSharedNine"],
    },
    {
        name: "a uniquely named class member surfaces in the workspace search for the class",
        source: "class ZqxZetaBoxedClassTen {\n  ZqxZetaMemberTen(): void {}\n}\n",
        query: "ZqxZetaClass",
        contains: ["ZqxZetaBoxedClassTen"],
    },
]);
