// Integration / completion -> definition -> references across module boundaries
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import { ensureCompilerConfigured, waitForCompletions, normalizeRange } from "../../harness";

let caseCounter = 0;

// Each scenario writes one fixture plus its companion modules into an isolated
// case directory, then walks the same chain the editor does: completion is
// offered at a cold position, the definition jump lands in the right module
// and references are found at the expected lines.
function chain(
    name: string,
    modules: ReadonlyArray<readonly [path: string, content: string]>,
    source: string,
    cursorLine: number,
    cursorCharacter: number,
    offered: string,
    defLine: number,
    referenceLines: readonly number[],
): void {
    caseCounter += 1;
    const caseId = caseCounter;
    const mainName = `integration-${caseId}.cp3`;

    suite(`Integration: imports + navigation (${name})`, () => {

        test(name, async () => {
            await ensureCompilerConfigured();
            const folder = vscode.workspace.workspaceFolders?.[0];
            assert.ok(folder, "integration tests need the run's workspace folder");
            const root = path.join(folder.uri.fsPath, "cases", "integration", String(caseId));
            fs.mkdirSync(root, { recursive: true });
            for (const [modulePath, moduleContent] of modules) {
                const target = path.join(root, modulePath);
                fs.mkdirSync(path.dirname(target), { recursive: true });
                fs.writeFileSync(target, moduleContent);
            }
            const mainPath = path.join(root, mainName);
            fs.writeFileSync(mainPath, source);
            const document = await vscode.workspace.openTextDocument(vscode.Uri.file(mainPath));
            const position = new vscode.Position(cursorLine, cursorCharacter);

            // Step 1: completion
            const labels = await waitForCompletions(document, position, [offered], 30_000);
            assert.ok(labels.includes(offered), `expected "${offered}" offered; got: ${labels.join(", ")}`);

            // Step 2: definition from the same position
            const locations = await vscode.commands.executeCommand<Array<{ range: unknown }>>(
                "_executeDefinitionProvider",
                document.uri,
                position,
            );
            const lines = (locations ?? []).map((location) => normalizeRange(location.range)?.start.line);
            assert.ok(lines.includes(defLine), `expected definition on line ${defLine}; got ${lines.join(",") || "none"}`);

            // Step 3: references to the same symbol
            const references = await vscode.commands.executeCommand<Array<{ range: unknown }>>(
                "_executeReferenceProvider",
                document.uri,
                position,
                { includeDeclaration: true },
            );
            const refLines = (references ?? []).map((location) => normalizeRange(location.range)?.start.line);
            for (const wanted of referenceLines) {
                assert.ok(refLines.includes(wanted), `expected a reference on line ${wanted}; got ${refLines.join(",") || "none"}`);
            }
        });
    });
}

const util = "export function combine(a: string, b: string): string {\n  return a + b;\n}\n";
const utilTwo = "export function one(): number {\n  return 1;\n}\nexport function two(): number {\n  return 2;\n}\n";
const utilClass = "export class Widget {\n  render(): void {}\n}\n";
const utilIFace = "export interface Sized {\n  size(): number;\n}\n";
const utilEnum = "export enum Mode { Fast, Slow }\n";
const utilAlias = "export type Radians = number;\n";

chain("imported function at the call site",
    [["util.cp3", util]],
    "import { combine } from \"./util.cp3\";\nconst joined = combine(\"a\", \"b\");\ncombine(\"c\", \"d\");\n",
    1, "const joined = ".length, "combine", 0, [1, 2]);

chain("imported function at the second call site",
    [["util.cp3", util]],
    "import { combine } from \"./util.cp3\";\ncombine(\"a\", \"b\");\n",
    1, 0, "combine", 0, [1]);

chain("imported class at the construction site",
    [["util.cp3", utilClass]],
    "import { Widget } from \"./util.cp3\";\nconst w = new Widget();\nw.render();\n",
    1, "const w = new ".length, "Widget", 0, [0, 1]);

chain("imported class at a type annotation",
    [["util.cp3", utilClass]],
    "import { Widget } from \"./util.cp3\";\nconst w: Widget = new Widget();\n",
    1, "const w: ".length, "Widget", 0, [0, 1]);

chain("imported interface at a parameter",
    [["util.cp3", utilIFace]],
    "import { Sized } from \"./util.cp3\";\nfunction f(s: Sized): void {}\nf;\n",
    1, "function f(s: ".length, "Sized", 0, [0, 1]);

chain("imported enum at a value position",
    [["util.cp3", utilEnum]],
    "import { Mode } from \"./util.cp3\";\nconst m = Mode.Fast;\n",
    1, "const m = ".length, "Mode", 0, [0, 1]);

chain("imported type alias at a type position",
    [["util.cp3", utilAlias]],
    "import { Radians } from \"./util.cp3\";\nconst r: Radians = 1;\n",
    1, "const r: ".length, "Radians", 0, [0, 1]);

chain("barrel re-export at the importing site",
    [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", utilTwo]],
    "import { one } from \"./barrel.cp3\";\nconst v = one();\n",
    1, "const v = ".length, "one", 0, [0, 1]);

chain("aliased named export at the importing site",
    [["alias.cp3", "function hidden(): number {\n  return 1;\n}\nexport { hidden as exposed };\n"]],
    "import { exposed } from \"./alias.cp3\";\nconst v = exposed();\n",
    1, "const v = ".length, "exposed", 0, [0, 1]);

chain("default export function at the call site",
    [["util.cp3", "export default function combine(a: string): string {\n  return a;\n}\n"]],
    "import combine from \"./util.cp3\";\nconst v = combine(\"a\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);

chain("renamed local import binding at the use site",
    [["util.cp3", util]],
    "import { combine as join } from \"./util.cp3\";\nconst v = join(\"a\", \"b\");\n",
    1, "const v = ".length, "join", 0, [0, 1]);

chain("deep nested companion import space",
    [["lib/deep/util.cp3", util]],
    "import { combine } from \"./lib/deep/util.cp3\";\nconst v = combine(\"a\", \"b\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);

chain("module used with multiple named imports in one line",
    [["util.cp3", utilTwo]],
    "import { one, two } from \"./util.cp3\";\nconst v = one() + two();\n",
    1, "const v = ".length, "one", 0, [0, 1]);

chain("module also exported from a second file does not confuse the jump",
    [["util.cp3", util], ["other.cp3", "export function combine(x: string): string {\n  return x;\n}\n"]],
    "import { combine } from \"./util.cp3\";\nconst v = combine(\"a\", \"b\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);

chain("class default export construction chain",
    [["util.cp3", "export default class Gadget {\n  fire(): void {}\n}\n"]],
    "import Gadget from \"./util.cp3\";\nconst g = new Gadget();\n",
    1, "const g = new ".length, "Gadget", 0, [0, 1]);

chain("a deep nested companion with a named type",
    [["lib/nested/shape.cp3", utilIFace]],
    "import { Sized } from \"./lib/nested/shape.cp3\";\nfunction f(s: Sized): void {}\n",
    1, "function f(s: ".length, "Sized", 0, [0, 1]);

chain("a function called through its local alias twice",
    [["util.cp3", util]],
    "import { combine as joinFn } from \"./util.cp3\";\nconst a = joinFn(\"x\", \"y\");\nconst b = joinFn(\"p\", \"q\");\n",
    1, "const a = ".length, "joinFn", 0, [1, 2]);

chain("chain through a relative path with dashes",
    [["lib/my-util.cp3", util]],
    "import { combine } from \"./lib/my-util.cp3\";\nconst v = combine(\"a\", \"b\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);

chain("chain through a relative path with underscores",
    [["lib/my_util.cp3", util]],
    "import { combine } from \"./lib/my_util.cp3\";\nconst v = combine(\"a\", \"b\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);

chain("an interface re-exported through a barrel at a use site",
    [["barrel.cp3", "export * from \"./base.cp3\";\n"], ["base.cp3", utilIFace]],
    "import { Sized } from \"./barrel.cp3\";\nfunction f(s: Sized): void {}\n",
    1, "function f(s: ".length, "Sized", 0, [0, 1]);

chain("a class from a folder index at a use site",
    [["pkg/index.cp3", utilClass]],
    "import { Widget } from \"./pkg/index.cp3\";\nconst w = new Widget();\n",
    1, "const w = new ".length, "Widget", 0, [0, 1]);

chain("an enum re-exported with an alias at a use site",
    [["barrel.cp3", "export { Mode as TrafficMode } from \"./base.cp3\";\n"], ["base.cp3", utilEnum]],
    "import { TrafficMode } from \"./barrel.cp3\";\nconst m = TrafficMode.Fast;\n",
    1, "const m = ".length, "TrafficMode", 0, [0, 1]);

chain("two imports from the same module in one file",
    [["util.cp3", utilTwo]],
    "import { one } from \"./util.cp3\";\nimport { two } from \"./util.cp3\";\nconst v = one() + two();\n",
    2, "const v = ".length, "one", 0, [0, 1, 2]);

chain("a nested companion with several declarations picks the right one",
    [["lib/util.cp3", utilTwo]],
    "import { two } from \"./lib/util.cp3\";\nconst v = two();\n",
    1, "const v = ".length, "two", 1, [0, 1]);

chain("a namespace default import used with member completion",
    [["util.cp3", util]],
    "import util from \"./util.cp3\";\nconst v = util;\n",
    1, "const v = ".length, "util", 0, [0, 1]);

chain("a module with a same-name export in two different files picks the imported one",
    [["lib/util.cp3", util], ["lib/other/util.cp3", "export function combine(z: number): number {\n  return z + 1;\n}\n"]],
    "import { combine } from \"./lib/util.cp3\";\nconst v = combine(\"a\", \"b\");\n",
    1, "const v = ".length, "combine", 0, [0, 1]);
