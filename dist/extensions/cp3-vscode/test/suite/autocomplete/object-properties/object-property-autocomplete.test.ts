// test/suite/autocomplete/object-properties/object-property-autocomplete.test.ts
//
// Example case: the shape every "Autocomplete" case follows. The fixture is
// written into the run's throwaway workspace, opened in the test VS Code
// window, and the built-in `_executeCompletionItemProvider` command (the same
// plumbing the editor's completion popup uses) is asked for the list at the
// cursor position. The cp3 language server answers; the extension host does
// the routing.

import * as assert from "node:assert/strict";
import * as vscode from "vscode";
import { cleanupFixtures, openWorkspaceCase, waitForCompletions } from "../../harness";

suite("Autocomplete", () => {
    suiteTeardown(() => cleanupFixtures());

    test("Object properties should autocomplete", async () => {
        const content = [
            "const user = {",
            '    name: "Test",',
            "    age: 25,",
            "};",
            "",
            "user.",
        ].join("\n");

        const document = await openWorkspaceCase("autocomplete-object.cp3", content, "completion");
        const line = content.split("\n").length - 1; // the "user." line
        const position = new vscode.Position(line, "user.".length);

        const labels = await waitForCompletions(document, position, ["name", "age"]);

        assert.ok(labels.includes("name"), `expected "name" in completions: ${labels.join(", ")}`);
        assert.ok(labels.includes("age"), `expected "age" in completions: ${labels.join(", ")}`);
    });
});
