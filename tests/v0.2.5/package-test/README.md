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
      "fileContains": [{"path": ".cp3/packages/x/index.cp3", "contains": "export class"}],
      "fileNotContains": [{"path": ".cp3/packages/x/index.cp3", "contains": "from \"./"}]
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

## Status (2026-11-15, `cp3c` 0.2.4 + the v0.2.5 dev tree)

`ok` = passes today (regression guard). `FAIL` = the desired behaviour is not
implemented yet; the case goes green when the defect is fixed.

| case | pins | status |
| --- | --- | --- |
| `pak/single-module-consume` | a single-module package survives build -> pack -> install -> consume | ok |
| `pak/multi-module-consume` | a **multi-module** package survives the same route | FAIL (CP3601: the installed stub imports `./widget.cp3`, which the pak does not ship) |
| `pak/reexport-consume` | a package whose entry re-exports (`export { X } from "./x.cp3";`) survives the route | FAIL (CP3601) |
| `pak/install-from-directory` | `cp3 install <dir>` yields a usable package | FAIL (the generated `index.cp3` has **no** export surface -> CP3304; the install step also crashes intermittently) |
| `pak/install-from-directory-multi-module` | the same for a multi-module directory | FAIL (CP3304) |
| `pak/install-from-directory-is-stable` | `cp3 install <dir>` must not crash the installer - 15 consecutive installs, all must succeed | FAIL (the installer crashes in roughly a third of the runs with 0xC0000005 / 0xC0000409; every failing iteration prints its reason) |
| `pak/reinstall-directory-over-existing` | installing the same directory twice must not crash | FAIL *probabilistically* (same crash) |
| `pak/reinstall-over-existing` | installing the same `.cp3pak` twice must work | ok |
| `pak/generated-stub-has-no-dangling-import` | the installed stub must not import a module the package does not ship | FAIL (the stub keeps `from "./widget.cp3"`) |
| `native/tcp-client-through-package` | a TCP client that lives **inside** the package must link in the consumer | FAIL (`ld.lld: undefined symbol: cp3::runtime::network::tcp::connectTcp`) |
| `native/tcp-client-symbol-named-in-app` | the documented workaround: the app names `connectTcp` itself | ok |
| `package/entry-without-exports` | a package entry without exports is reported at check time (CP3511) | ok |

### Notes for whoever fixes these

* The crash pin is probabilistic per run but stable in volume: the case
  itself installs the same directory 15 times, and any failing iteration
  prints its own reason (the exit code, in hex for signal deaths such as
  0xC0000005 / 0xC0000409, plus the last output line). Run it a few times
  (`python tests/v0.2.5/package-test/runner.py --filter install-from-directory`)
  to see the crash pattern. `pak/reinstall-directory-over-existing` pins the
  second-install-over-a-partial-install crash separately.
* `cp3 install <dir>` and `cp3 install <pak>` take different code paths: the
  directory route generates the consumer-facing `index.cp3` itself and produces
  an **empty** surface today, while the pak route ships a surface generated at
  pack time. Both must end up with the same, usable surface.
* The native-backend selection scans the **consumer's** emitted text for
  markers (`connectTcp(`, `TcpServer`, ...). A reference that only exists
  inside an installed package is invisible to that scan, so the TCP runtime
  translation units are not linked and the consumer fails at link time.
* The language-level half of this work lives in `tests/cases/v0-2-5/`.
