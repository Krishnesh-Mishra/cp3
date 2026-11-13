// Signature help / std members and edge cases
import { signatureHelpCases } from "../../harness";

signatureHelpCases("Signature help: std members", [
    {
        name: "std/math variadic max shows the variadic signature",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__max(1, 9, 3);\n",
        contains: ["max", "values"],
    },
    {
        name: "std/math variadic min shows the variadic signature",
        source: "import Math from \"std/math\";\nconst value = Math.__CURSOR__min(1, 9, 3);\n",
        contains: ["min", "values"],
    },
    {
        name: "std/path join shows its variadic parts signature",
        source: "import path from \"std/path\";\nconst joined = path.__CURSOR__join(\"a\", \"b\", \"c\");\n",
        contains: ["join", "parts"],
    },
    {
        name: "std/console input shows the optional prompt parameter",
        source: "import console from \"std/console\";\nconst answer = console.__CURSOR__input(\"Name?\");\n",
        contains: ["input", "prompt"],
    },
    {
        name: "std/string repeat shows the times parameter",
        source: "const out = \"ab\".__CURSOR__repeat(2);\n",
        contains: ["repeat", "count"],
    },
    {
        name: "std/string slice shows its optional range parameters",
        source: "const text = \"hello\";\nconst part = text.__CURSOR__slice(1);\n",
        contains: ["slice"],
    },
    {
        name: "std/string padStart shows the width parameter",
        source: "const text = \"hi\";\nconst padded = text.__CURSOR__padStart(4);\n",
        contains: ["padStart"],
    },
    {
        name: "std/json stringify shows the documented signature",
        source: "import JSON from \"std/json\";\nconst text = JSON.__CURSOR__stringify(42);\n",
        contains: ["stringify"],
    },
    {
        name: "std/json parse shows the documented signature",
        source: "import JSON from \"std/json\";\nconst value = JSON.__CURSOR__parse(\"42\");\n",
        contains: ["parse"],
    },
    {
        name: "std/fs readFileSync shows its documented signature",
        source: "import fs from \"std/fs\";\nconst content = fs.__CURSOR__readFileSync(\"file.txt\");\n",
        contains: ["readFileSync"],
    },
]);
