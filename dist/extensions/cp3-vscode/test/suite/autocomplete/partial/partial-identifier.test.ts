// Autocomplete / partial identifier filtering
import { completionCases } from "../../harness";

completionCases("Autocomplete: partial identifier filtering", [
    {
        name: "partial name narrows to the single match",
        source: "const apple = 1;\nconst apricot = 2;\nconst banana = 3;\nconst fruit = ap__CURSOR__;\n",
        expectPresent: ["apple", "apricot"],
        expectAbsent: ["banana"],
    },
    {
        name: "single-character prefix offers all matches",
        source: "const alpha = 1;\nconst alpine = 2;\nconst other = 3;\nconst value = a__CURSOR__;\n",
        expectPresent: ["alpha", "alpine"],
        expectAbsent: ["other"],
    },
    {
        name: "member access with prefix offers only matching member",
        source: "const user = { name: \"Test\", nickname: \"nick\", age: 25 };\nuser.na__CURSOR__\n",
        expectPresent: ["name", "nickname"],
        expectAbsent: ["age"],
    },
    {
        name: "prefix filtering is case-insensitive for the first letter",
        source: "const apple = 1;\nconst value = App__CURSOR__;\n",
        expectPresent: ["apple"],
    },
    {
        name: "no matches yields an empty or prefix-free list",
        source: "const apple = 1;\nconst value = zz__CURSOR__;\n",
        expectAbsent: ["apple"],
    },
    {
        name: "partial Math member offers only matching std members",
        source: "import Math from \"std/math\";\nMath.fl__CURSOR__\n",
        expectPresent: ["floor"],
        expectAbsent: ["abs"],
    },
    {
        name: "partial array method offers the matching family",
        source: "const items = [3, 1, 2];\nitems.so__CURSOR__\n",
        expectPresent: ["sort"],
    },
    {
        name: "prefix with underscore keeps identifier semantics",
        source: "const __internal__ = 1;\nconst other = __CURSOR__;\n",
        expectPresent: ["__internal__"],
    },
    {
        name: "exact name followed by cursor does not offer itself twice",
        source: "const count = 5;\nconst value = cou__CURSOR__;\n",
        expectPresent: ["count"],
    },
    {
        name: "member prefix distinguishes wide from narrow candidates",
        source: "const player = { score: 1, speed: 2, stamina: 3 };\nplayer.s__CURSOR__\n",
        expectPresent: ["score", "speed", "stamina"],
    },
]);
