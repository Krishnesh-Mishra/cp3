# `tests/cases/v0-2-6/package-e2e/` - the `.cp3pak` **source package format** end-to-end pins

End-to-end pins for the v0.2.6 change of the `.cp3pak` container: a
multi-module package is now packed as its **entry plus the full
relative-import closure** (verbatim `.cp3` sources at their package-relative
paths, `formatVersion` bumped), and `cp3c install` compiles/uses them as
ordinary units.

These cases exist because of a real, previously broken flow: a package packed
by `cp3c build` carried only a generated signatures stub (`index.cp3`) whose
relative imports (kept verbatim) pointed at files that were never shipped.
`CP3601` ("cannot find module './x.cp3'") fired on every import of the stub
in the consumer, and exported types living behind those modules were
unresolvable (`CP3302`). The fixtures here pin the fixed behavior in
synthetic form.

All package and module names are synthetic (`pak_tide`, `pak_closure`,
`pak_solo`, `pak_escape`, `pak_promptkeep`, `legacy_tide`). No real project
or third-party library is referenced.

## Verification invocation

```bash
python tests/v0.2.5/package-test/runner.py --cases tests/cases/v0-2-6/package-e2e
```

**The maintainer runs this.** The agents that authored these fixtures never
executed the runner, `cp3c`, a build, or any other process - fixture
correctness here has been reasoned, not observed. Expect the multi-module /
closure / single-module / install-prompt cases to pass only with the format
fix in place (that is the point of a regression pin); the legacy-format and
escape cases are green only after the clean break lands.

### One-time pre-step for `pak/legacy-format-reject/`

`pak/legacy-format-reject/app/legacy-format-v1.cp3pak` is a binary member
(gzip + USTAR tar) and is **not checked in** (`.gitignore` ignores
`*.cp3pak`). Materialize it once before the suite:

```bash
python tests/cases/v0-2-6/package-e2e/pak/legacy-format-reject/make_legacy_pak.py
```

Deterministic (mtime-pinned tar headers and gzip stream), mirrors
`tests/cli/run_cli_tests.py`.

## What each fixture pins

| fixture | pins |
| --- | --- |
| `pak/multifile-consume/` | The full broken flow: multi-module package (class import, a sibling imported by the entry AND by another module - transitive depth >= 2, type-only imports of an interface + a type alias, utility functions, a barrel re-export `export { x } from "./describe.cp3"`, an aliased relative import) packs, installs, and the consumer compiles/runs clean. The CP3601/CP3302 regression pin. Before the fix, step 3 fails with CP3601 on every relative import of the installed stub. |
| `pak/closure-excludes-unreferenced/` | The pak ships exactly the closure: `src/unused.cp3` (never imported) must not appear in the archive or the installed tree. Runner cannot assert archive members - see the fixture README; install-tree assertions are the harness-expressible proxy, exact member list is a documented manual check. |
| `pak/package-root-escape/` | `cp3 build` on a package whose entry imports a module outside the package root (`pkg/src/main.cp3` -> `../../outside.cp3`, i.e. the case-root file) fails (non-zero; stderr contains `escapes the package root`) and produces no `.cp3pak`. |
| `pak/legacy-format-reject/` | Clean break: `cp3 install` of a hand-fabricated formatVersion 1 pak (C++ members, no `.cp3` sources) fails non-zero; stderr contains `unsupported .cp3pak formatVersion`; nothing lands in `.cp3/packages/`. |
| `pak/install-prompt-non-tty/` | Non-tty install: succeeds, and the source `.cp3pak` still exists afterwards (`filesExist` on `../pkg/.cp3/out/pak_promptkeep.cp3pak`). The interactive prompt cannot be exercised here; not pinned (wording unlanded) - manual verification documented in the fixture README. |
| `pak/single-module-consume/` | Regression guard for the previously-working path: single-file package, build -> install -> run green. |

All fixtures follow the scenario layout: `case.json` + `pkg/` (the package
project) + `app/` (the consumer project). The runner copies the fixture to a
temp directory and executes each step there.

## What the harness can express vs. what is documented for the maintainer

The scenario runner (`tests/v0.2.5/package-test/runner.py`) supports:
`expectExit` (exact integer only), `stdoutContains`/`stdoutNotContains`,
`stderrContains`/`stderrNotContains`, `filesExist`/`filesAbsent` (relative to
the step's cwd), `fileContains`/`fileNotContains` (text files only),
`repeat`. It does **not** support: archive inspection, running arbitrary
commands (no shell steps), asserting "any non-zero exit", or capturing stdin
interactivity.

Documented for the maintainer (not harness-expressible):

- **Archive member list** (`closure-excludes-unreferenced`): exact members of
  the produced pak - must contain the closure at package-relative paths and
  must NOT contain `src/unused.cp3`. Command in the fixture README
  (`tarfile` one-liner). The install-step `filesExist`/`filesAbsent` pins
  proxy for it and encode the "exact folder structure" promise of the v0.2.6
  change; if the landed layout flattens paths, adjust those three paths, not
  the intent.
- **Interactive install prompt** (`install-prompt-non-tty`): the prompt
  question is printed only on a tty; answer `y`/`n` behave per the landed
  spec. Not exercisable in this harness (never a tty); the non-tty path is
  what `case.json` pins. Question text deliberately unpinned.
- **Exact exit codes**: `package-root-escape` pins exit 1 (the
  `kExitCompileError` convention); `legacy-format-reject` pins exit 1 for
  the same reason. If the landed fix reports these paths with a different
  code, change only `expectExit` and keep the stderr-substring and
  `filesAbsent` pins - those carry the intent.
- **Prompt/packaging interplay of a directory install** (`cp3 install <dir>`)
  is out of scope here; this suite covers the `.cp3pak` file path.

## Post-landing note (updated by the maintainer after the fix landed)

The clean break LANDED in the same change series that ships this suite, and
the implementation agent updated the previously known-red files in place to
the new format. They are no longer red by design:

- `tests/v0.2.5/package-test/cases/pak/multi-module-consume/` and
  `reexport-consume/` - expected green now (the bug they pinned, CP3601 on
  the installed stub, is fixed); re-verified by the maintainer.
- `tests/v0.2.5/package-test/cases/pak/generated-stub-has-no-dangling-import/` -
  the stub no longer exists; the fixture now pins that the installed package
  carries ONLY the authored `.cp3` sources plus `package.json` and
  `cp3pak.json` (`"formatVersion": 2`, `main` as authored) and no `pak/`.
- `tests/v0.2.5/package-test/README.md` - status table updated accordingly.
- `tests/cli/run_cli_tests.py` fixture builders (`build_cp3pak`,
  `build_excluded_source_cp3pak`, `build_cached_cp3pak`,
  `build_native_cp3pak`) now emit `formatVersion` 2; the removed builders
  (`build_source_cp3pak` and the `local-dep-source` / `native-source-dep`
  members) are gone, along with the recompile cases that pinned binary-cache
  production.
- `tests/cli/cases/tool/install.json`, `tests/cli/cases/tool/recompile.json`
  and `tests/cli/cases/core/build.json` - expectations rewritten to the new
  format (`unsupported .cp3pak formatVersion` wording with
  `rebuild the package with a newer cp3c`, archive member lists for source
  packages, `build.cacheBinary is ignored` warnings).
- `tests/cases/v0-2-6/package-e2e/` itself is untouched for Git-status
  stability (it is this suite's fixture root).

The language corpus (`tests/cases/**` outside `v0-2-6/`) is unaffected.
