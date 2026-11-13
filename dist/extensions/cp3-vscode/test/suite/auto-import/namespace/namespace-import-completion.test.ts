// Auto-import / namespace imports
import { autoImportCases } from "../../harness";

autoImportCases("Auto-import: namespace imports", [
    {
        name: "std/math members complete after the imported namespace name",
        source: "import Math from \"std/math\";\nMath.__CURSOR__\n",
        expectOffered: "abs",
    },
    {
        name: "std/path members complete after the imported namespace name",
        source: "import path from \"std/path\";\npath.__CURSOR__\n",
        expectOffered: "join",
    },
    {
        name: "imported namespace does not re-offer members on the bare identifier",
        source: "import Math from \"std/math\";\nconst value = __CURSOR__\n",
        expectOffered: "abs",
        expectNoItem: true,
    },
    {
        name: "std/console completes console.log after the namespace",
        source: "import console from \"std/console\";\nconsole.__CURSOR__\n",
        expectOffered: "log",
    },
    {
        name: "typing a stdlib namespace without an import offers the auto-import",
        source: "const value = Math.__CURSOR__\n",
        expectOffered: "abs",
    },
]);
