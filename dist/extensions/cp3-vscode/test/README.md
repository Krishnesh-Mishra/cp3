# cp3-vscode test suite

Extension-host tests for the cp3 VS Code extension. They run inside a real,
throwaway VS Code (`@vscode/test-electron`) and drive the **bundled language
server** through the same `vscode.execute*Provider` commands the editor uses,
so a passing case means the feature actually works for a user — not just that
a mock agrees.

## Layout

```
test/
  runTest.mjs        launcher: downloads VS Code, isolated user-data/extensions/workspace dirs
  run.cjs            in-host mocha runner (TDD); discovers out/test/suite/**/*.test.js recursively
  tsconfig.json      type-checks the suite (npm run typecheck)
  suite/
    harness.ts       shared helpers: fixture files, cp3c resolution, completion polling, cleanup
    <category>/      one folder per feature area, matching the distribution tables
      README.md      scope + target case count for the area
      <subcategory>.test.ts
```

`out/test/suite/` mirrors `test/suite/` after `npm run compile:test`; the
runner picks up every `*.test.js` recursively, so dropping a new file or
folder in is all that is needed to add it to the main suite.

## Running

```bash
npm test
```

compiles the extension, compiles the tests, launches VS Code and runs every
case. Exits non-zero when any case fails.

## Writing a case

```ts
import * as assert from "node:assert/strict";
import * as vscode from "vscode";
import { cleanupFixtures, openFixture, waitForCompletions } from "../harness";

suite("Autocomplete", () => {
    suiteTeardown(() => cleanupFixtures());

    test("…", async () => {
        const document = await openFixture("name.cp3", "const user = {\n    name: 1\n};\nuser.\n");
        const position = new vscode.Position(3, "user.".length);
        const labels = await waitForCompletions(document, position, ["name"]);
        assert.ok(labels.includes("name"));
    });
});
```

Rules of the suite:

- One file per subcategory: `<subcategory>.test.ts`. Keep `suite("…")` titles
  equal to the category; file names say what is covered.
- Fixtures are real `.cp3` files written to a temp dir by `writeCp3Fixture` /
  `openFixture` — never untitled documents (the server only sees `file://`).
- The language server needs a moment to start and parse; never assert on the
  first response directly. Use the polling helpers in `harness.ts`.
- `cleanupFixtures()` in `suiteTeardown` removes the temp files.
- `harness.ts` pins `cp3.compilerPath` to the repository's release build
  (`build/windows-release/bin/cp3c.exe` on Windows, falling back to
  `build/out/windows-x64/stage/cp3/bin/cp3c.exe`, then `build/bin/cp3c.exe`),
  so the suite never depends on what is on PATH. The setting is written to
  the isolated profile and discarded after the run.
- Extension-only behavior (organize imports, run command, schema association,
  completion ranking of workspace exports) is tested in the same harness;
  server behavior is exercised through the provider commands, not by mocking.
