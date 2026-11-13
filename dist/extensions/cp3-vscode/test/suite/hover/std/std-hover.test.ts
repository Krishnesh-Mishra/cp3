// Hover / std members and documented builtins
import { hoverCases } from "../../harness";

hoverCases("Hover: std members and builtins", [
    {
        name: "Math.abs hover shows the std function signature",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__abs(-1);\n",
        contains: ["abs"],
    },
    {
        name: "Math.abs hover documents what it computes",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__abs(-4);\n",
        contains: ["absolute"],
    },
    {
        name: "console.log hover shows the documented std member",
        source: "import console from \"std/console\";\nconsole.__CURSOR__log(\"hello\");\n",
        contains: ["log"],
    },
    {
        name: "console.log hover carries the std description",
        source: "import console from \"std/console\";\nconsole.__CURSOR__log(\"hello\");\n",
        contains: ["Prints"],
    },
    {
        name: "path.join hover shows the std member name",
        source: "import path from \"std/path\";\nconst joined = path.__CURSOR__join(\"a\", \"b\");\n",
        contains: ["join"],
    },
    {
        name: "Math.floor hover shows the member name",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__floor(3.7);\n",
        contains: ["floor"],
    },
    {
        name: "Math.ceil hover shows the member name",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__ceil(3.2);\n",
        contains: ["ceil"],
    },
    {
        name: "Math.max hover shows the member name",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__max(1, 9);\n",
        contains: ["max"],
    },
    {
        name: "string toUpperCase hover shows the method name",
        source: "const text = \"hi\";\nconst up = text.__CURSOR__toUpperCase();\n",
        contains: ["toUpperCase"],
    },
    {
        name: "a string hover on toUpperCase shows its result type",
        source: "const text = \"hi\";\nconst up = text.__CURSOR__toUpperCase();\n",
        contains: ["string"],
    },
]);
