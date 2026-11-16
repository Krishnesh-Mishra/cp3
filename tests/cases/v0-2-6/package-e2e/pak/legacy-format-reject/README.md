# pak/legacy-format-reject

Pins the **clean break**: a legacy `.cp3pak` (`formatVersion` 1, C++-source
members, no `.cp3` sources) must be rejected by `cp3c install` - non-zero
exit, stderr carrying the exact substring `unsupported .cp3pak formatVersion`,
and nothing installed into `.cp3/packages/`. It pins that formatVersion 1 is
dead: rebuild with a newer `cp3c`; do not resurrect reader support.

## The `.cp3pak` is NOT checked in

`app/legacy-format-v1.cp3pak` is a binary (gzip + USTAR tar) and `.gitignore`
ignores `*.cp3pak`, so it cannot be committed anyway. The agents that wrote
this fixture never ran code. Run the committed helper once, then the suite:

```bash
python tests/cases/v0-2-6/package-e2e/pak/legacy-format-reject/make_legacy_pak.py
python tests/v0.2.5/package-test/runner.py --cases tests/cases/v0-2-6/package-e2e
```

The helper is deterministic (tar member headers and the gzip stream are
pinned to mtime 0) and prints the archive's byte size; the member layout
mirrors `tests/cli/run_cli_tests.py` (`write_cp3pak` / `build_cp3pak`, both
`formatVersion` 2:
USTAR tar inside gzip, mtime zero):

- `cp3pak.json` - `{"formatVersion": 1, "name": "legacy_tide", "version": "1.2.3", "main": "index.cp3"}`
- `package.json` - the author's manifest, verbatim
- `pak/src/tide.cpp`, `pak/src/tide.hpp` - the transpiled C++ (self-contained,
  exactly what a v1 pak carried)
- **no `.cp3` sources** - not even `index.cp3`, the strongest form of "the old
  container layout is unusable without its sources".

`pkg/` holds the synthetic source `tide.cp3` the fabricated C++ members derive
from; it documents provenance only and is never built or consumed.

## Why install is expected to fail with exit 1

`installLocalOne` -> `inspectPackageSource` -> `readCp3pak`: the manifest
declares `formatVersion: 1`, and a v0.2.6 `cp3c` no longer accepts it, so
`parseManifest` fails with `unsupported .cp3pak formatVersion 1 (...)` before
any member is processed or any directory is created. Exit code 1 is this
codebase's `kExitCompileError` convention (the runner can only express an
exact code, not "any non-zero"); the substring pin is the load-bearing one.
`filesAbsent: .cp3/packages/legacy_tide` pins that nothing was installed.

If the landed fix changes the wording after `unsupported .cp3pak formatVersion`,
adjust the substring - not the intent (clean break, no fallback path).
