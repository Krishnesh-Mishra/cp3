# `tests/v0.2.5/package-test/` - the packaging regression corpus

A package is not one command: it is built, packed, installed and then
consumed by another project. The language corpus (`tests/cases/**`) runs one
command per case, so it cannot express that. This suite drives a **scenario**
instead - an ordered list of steps, each a `cp3c` invocation with its own
expectations.

The cases pin the behaviour a real package needs: a multi-module package
that a consumer can install and use, an installer that does not crash, and a
native backend that is linked when only the *package* uses it.

## Running

```bash
python tests/v0.2.5/package-test/runner.py [--verbose] [--filter SUBSTR] [--list]
```

The suite also runs as part of the main harness, after the language corpus:

```bash
python tests/runner/run_tests.py --filter v0-2-5          # corpus + packaging
python tests/runner/run_tests.py --no-package-tests       # corpus only
python tests/runner/run_tests.py --package-test-args "--filter pak"
```

## Layout

```
tests/v0.2.5/package-test/
  runner.py                the scenario runner
  cases/<category>/<name>/
    case.json              the steps and their expectations
    pkg/                   the package project (package.json + src/*.cp3)
    app/                   the consumer project (package.json + src/index.cp3)
```

Each case runs in a unique temp copy of its fixture; the checked-in fixture is
never dirtied.

## `case.json`

```json
{
  "steps": [
    {
      "cwd": "pkg",
      "args": ["build", "src/index.cp3"],
      "expectExit": 0,
      "stdoutContains": [],
      "stdoutNotContains": [],
      "stderrContains": ["packed"],
      "stderrNotContains": [],
      "filesExist": [],
      "filesAbsent": [],
      "fileContains": [{"path": ".cp3/packages/x/src/index.cp3", "contains": "export class"}],
      "fileNotContains": [{"path": ".cp3/packages/x/src/index.cp3", "contains": "from \"./"}]
    }
  ]
}
```

`cwd` is relative to the fixture root (`pkg`, `app`, or `.`). The first failing
step stops the case.

A step may also carry `"repeat": N` (integer >= 1, default 1): the same
command runs N times in the same directory and **every** iteration is asserted
independently - the reasons for every failing iteration are logged
(`step 3 (install ../pkg) iteration 7/15: exit code -1073741819
(0xC0000005), expected 0 (last: …)`), the pass/fail verdict is all-or-nothing
and the failing iterations do not stop the remaining ones. File assertions
(`filesExist`/`filesAbsent`/`fileContains`/`fileNotContains`) are checked on
the last iteration only.

## Status (2026-11-16, after the `.cp3pak` source-package change)

`ok` = regression guard. The v0.2.5 "FAIL" rows pinned the prebuilt-stub
model, which was deleted: `.cp3pak` is now `formatVersion` 2 and carries the
authored `.cp3` sources, so the consumer compiles them like local units and
the stub/CP3601/CP3304 failure modes no longer exist. The rows marked `ok*`
were FAIL before the change and are expected to pass now; the fix could not
be executed in this environment (no compiler runs), so the suite must be
re-run by the maintainer.

| case | pins | status |
| --- | --- | --- |
| `pak/single-module-consume` | a single-module package survives build -> pack -> install -> consume | ok |
| `pak/multi-module-consume` | a **multi-module** package survives the same route | ok* |
| `pak/reexport-consume` | a package whose entry re-exports (`export { X } from "./x.cp3";`) survives the route | ok* |
| `pak/install-from-directory` | `cp3 install <dir>` yields a usable package | ok* |
| `pak/install-from-directory-multi-module` | the same for a multi-module directory | ok* |
| `pak/install-from-directory-is-stable` | `cp3 install <dir>` must not crash the installer - 15 consecutive installs, all must succeed | ok* |
| `pak/reinstall-directory-over-existing` | installing the same directory twice must not crash | ok* |
| `pak/reinstall-over-existing` | installing the same `.cp3pak` twice must work | ok |
| `pak/generated-stub-has-no-dangling-import` | the installed package carries ONLY the authored `.cp3` sources (no stub, no `pak/` members, `cp3pak.json` + `package.json` present) | ok* |
| `native/tcp-client-through-package` | a TCP client that lives **inside** the package must link in the consumer | ok* (the marker scan folds the package units' text, so the TCP runtime TUs are seen and linked) |
| `native/tcp-client-symbol-named-in-app` | the documented workaround: the app names `connectTcp` itself | ok |
| `package/entry-without-exports` | a package entry without exports is reported at check time (CP3511) | ok |

### Notes for whoever fixes these

* `.cp3pak` is `formatVersion` 2: it carries `cp3pak.json` (first), `package.json` verbatim and the entry's reachable relative-import closure as plain `.cp3` files. There is no `index.cp3` stub, no `pak/*.cpp`, and no cached binary; the consumer compiles the installed sources like its own (the marker scan folds the unit TUs' text, which is what lets `std/network` inside a package link). Old (`formatVersion` 1) archives are rejected with `unsupported .cp3pak formatVersion`.
* `cp3 install <dir>` installs the source tree the same way a pak does (package.json + the closure + build.files), so directory installs and pak installs produce the same layout. It no longer compiles at install time.
* The language-level half of this work lives in `tests/cases/v0-2-5/`.
