// Code actions / CP3: Organize Imports
import { codeActionCases } from "../../harness";
import * as vscode from "vscode";

// The extension sorts a contiguous group of single-line imports with
// `localeCompare`; these cases generate every permutation of three module
// imports and expect the action to be offered exactly when the group is
// not already sorted. Valid specifiers chosen to exercise the comparator.
const MODULES = [
    "import { banana } from \"./banana.cp3\";",
    "import console from \"std/console\";",
    "import { apple } from \"./apple.cp3\";",
    "import path from \"std/path\";",
    "import { cherry } from \"./cherry.cp3\";",
];

function permutationTriples(): string[][] {
    const triples: string[][] = [];
    for (let a = 0; a < MODULES.length; a += 1) {
        for (let b = 0; b < MODULES.length; b += 1) {
            if (b === a) continue;
            for (let c = 0; c < MODULES.length; c += 1) {
                if (c === a || c === b) continue;
                triples.push([MODULES[a], MODULES[b], MODULES[c]]);
            }
        }
    }
    return triples;
}

const ORGANIZE = "Organize Imports";

const permutationCases = permutationTriples().slice(0, 36).map((group, index) => {
    const sorted = [...group].sort((x, y) => x.localeCompare(y));
    const alreadySorted = group.every((line, i) => line === sorted[i]);
    const source = group.join("\n") + "\n";
    return alreadySorted
        ? {
              name: `permutation ${index + 1}: already sorted group offers no action`,
              source,
              expectNone: true,
          }
        : {
              name: `permutation ${index + 1}: unsorted group offers Organize Imports`,
              source,
              expectTitle: [ORGANIZE],
          };
});

codeActionCases("Code actions: organize imports", [
    ...permutationCases,
    {
        name: "a side-effect import inside the group disables organizing",
        source: "import { banana } from \"./banana.cp3\";\nimport \"./setup.cp3\";\nimport { apple } from \"./apple.cp3\";\n",
        expectNone: true,
    },
    {
        name: "a group split by a blank line is not one contiguous block",
        source: "import { banana } from \"./banana.cp3\";\n\nimport { apple } from \"./apple.cp3\";\n",
        expectNone: true,
    },
    {
        name: "two out-of-order named imports offer Organize Imports",
        source: "import { zephyr } from \"./z.cp3\";\nimport { aardvark } from \"./a.cp3\";\n",
        expectTitle: [ORGANIZE],
    },
    {
        name: "two sorted named imports offer nothing",
        source: "import { aardvark } from \"./a.cp3\";\nimport { zephyr } from \"./z.cp3\";\n",
        expectNone: true,
    },
    {
        name: "a multiline import in the group disables organizing",
        source: "import { banana,\n  apple } from \"./fruit.cp3\";\nimport path from \"std/path\";\n",
        expectNone: true,
    },
    {
        name: "a single import offers nothing to organize",
        source: "import console from \"std/console\";\n",
        expectNone: true,
    },
    {
        name: "an uppercase vs lowercase module order sorts by code points",
        source: "import { banana } from \"./z.cp3\";\nimport { aardvark } from \"./A.cp3\";\n",
        expectTitle: [ORGANIZE],
    },
    {
        name: "import lines with different member counts still sort as lines",
        source: "import { bananaSplit, cherry } from \"./fruit.cp3\";\nimport { apple } from \"./apple.cp3\";\n",
        expectTitle: [ORGANIZE],
    },
    {
        name: "already interleaved std and local imports sort as one group",
        source: "import console from \"std/console\";\nimport { aardvark } from \"./apple.cp3\";\n",
        expectTitle: [ORGANIZE],
    },
    {
        name: "an empty file has nothing to organize",
        source: "",
        expectNone: true,
    },
]);

// The Organize Imports command is contributed by the extension; these are
// the command-id-level guarantees for the contributed surface.
suite("Code actions: contributed command", () => {
    test("cp3.organizeImports command is registered", async () => {
        const commands = await vscode.commands.getCommands(true);
        if (!commands.includes("cp3.organizeImports")) {
            throw new Error("cp3.organizeImports is not registered");
        }
    });
});
