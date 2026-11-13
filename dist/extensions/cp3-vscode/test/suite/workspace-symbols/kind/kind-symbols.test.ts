// Workspace symbols / classes, interfaces, enums and types
import { workspaceSymbolCases } from "../../harness";

workspaceSymbolCases("Workspace symbols: kinds", [
    {
        name: "a uniquely named class is found by its unique prefix",
        source: "class ZqxLambdaClassEleven {}\n",
        query: "ZqxLambdaClassEleven",
        contains: ["ZqxLambdaClassEleven"],
    },
    {
        name: "a uniquely named interface is found by its unique prefix",
        source: "interface ZqxMuInterfaceTwelve {\n  size(): number;\n}\n",
        query: "ZqxMuInterfaceTwelve",
        contains: ["ZqxMuInterfaceTwelve"],
    },
    {
        name: "a uniquely named enum is found by its unique prefix",
        source: "enum ZqxNuEnumThirteen { Alpha, Beta }\n",
        query: "ZqxNuEnumThirteen",
        contains: ["ZqxNuEnumThirteen"],
    },
    {
        name: "a uniquely named type alias is found by its unique prefix",
        source: "type ZqxXiAliasFourteen = number;\n",
        query: "ZqxXiAliasFourteen",
        contains: ["ZqxXiAliasFourteen"],
    },
    {
        name: "a uniquely named generic class is found by its unique prefix",
        source: "class ZqxOmicronGenericFifteen<T> {\n  value: T = 0 as T;\n}\n",
        query: "ZqxOmicronGenericFifteen",
        contains: ["ZqxOmicronGenericFifteen"],
    },
    {
        name: "a uniquely named abstract class is found by its unique prefix",
        source: "abstract class ZqxPiAbstractSixteen {\n  abstract run(): void;\n}\n",
        query: "ZqxPiAbstractSixteen",
        contains: ["ZqxPiAbstractSixteen"],
    },
    {
        name: "a uniquely named default-exported class is found by its unique prefix",
        source: "export default class ZqxRhoDefaultSeventeen {}\n",
        query: "ZqxRhoDefaultSeventeen",
        contains: ["ZqxRhoDefaultSeventeen"],
    },
    {
        name: "a uniquely named interface member is not a workspace symbol root",
        source: "interface ZqxSigmaHolderEighteen {\n  ZqxSigmaMemberNineteen(): void;\n}\n",
        query: "ZqxSigmaHolderEighteen",
        contains: ["ZqxSigmaHolderEighteen"],
    },
    {
        name: "a uniquely named class with an implements clause is found",
        source: "interface Sized {\n  size(): number;\n}\nclass ZqxTauSizedTwenty implements Sized {\n  size(): number { return 1; }\n}\n",
        query: "ZqxTauSizedTwenty",
        contains: ["ZqxTauSizedTwenty"],
    },
    {
        name: "a uniquely named const of class type is found",
        source: "class ZqxUpsilonBoxedTwentyOne {}\nconst ZqxUpsilonInstanceTwentyOne = new ZqxUpsilonBoxedTwentyOne();\n",
        query: "ZqxUpsilonInstanceTwentyOne",
        contains: ["ZqxUpsilonInstanceTwentyOne"],
    },
]);
