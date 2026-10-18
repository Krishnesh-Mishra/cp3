# cp3 test cases

This directory holds the conformance cases for `cp3c`. The format below is
**frozen** - the harness (`tests/runner/run_tests.py`) and every existing case
already depend on it. Do not change it; add new cases instead.

## Layout

```
tests/cases/
  <category>/                 one folder per feature area
    <name>.cp3                the source under test
    <name>.expect.json        the expected result (frozen schema below)
```

A case is the **pair** `<category>/<name>.cp3` + `<category>/<name>.expect.json`.
The `.expect.json` file is what the harness discovers (`tests/cases/**/*.expect.json`);
the sibling `.cp3` is derived from the JSON filename.

> **Ownership:** each `<category>/` folder maps to one feature area; a wave
> adds new cases inside the areas it touches. Crossing into another area needs
> a lead grant recorded here: the typed-arrays landing (2026-10-18) added 20
> new files to `arrays-strings/` under such a grant (new files only, no
> existing case edited).

## `.expect.json` schema

```jsonc
{
  // Required. How the case is invoked:
  //   "run"   -> build the program, execute it, compare stdout + exitCode
  //   "check" -> frontend only (parse + type-check), no C++ generation/exec
  "mode": "run",

  // Expected process stdout. Exact match after newline normalization
  // (CRLF/CR are folded to LF). Omit for "".
  "stdout": "18\n",

  // Expected process exit code. Omit for 0.
  "exitCode": 0,

  // Optional. When present, the case asserts a *compile error* instead:
  // the exit code must match and at least one diagnostic must match all of
  // these constraints. Omit for a success case.
  "error": {
    "code": "CP3408",     // CP3xxxx code; see wildcard rules below
    "line": 3,            // 1-based; omit/null to accept any line
    "col": 1,             // 1-based; omit/null to accept any column
    "contains": "const"   // substring that must appear in the diagnostic text
  }
}
```

All fields except `mode` are optional. Unknown fields are ignored.

### Modes

* **`run`** - the harness runs `cp3c run <file.cp3>`, then compares the process
  `stdout` to `"stdout"` and the exit code to `"exitCode"`.
* **`check`** - the harness runs `cp3c check <file.cp3>` (frontend only). The
  same stdout/exitCode comparison applies.

If `error` is present in either mode, the harness instead requires a **compile
error**: the exit code must match `"exitCode"` (default 1) and a diagnostic
matching `error` must appear on stdout/stderr.

### Error-code wildcards

Because some cases assert "any diagnostic of this family", the `code` field
supports wildcards:

* `x` / `X` / `?` - one character; a **run of two or more** means "any run"
* `*` - any run

So the frozen placeholder `"CP3xxxx"` matches any real code such as `CP3408`,
while `"CP3408"` matches exactly. Prefer the exact code once the real diagnostic
exists.

### Diagnostic line format

The harness reads diagnostics from the combined stdout/stderr. The canonical
shape the CLI is expected to emit is:

```
<path>:<line>:<col>: <severity>: <CODE>: <message>
```

for example:

```
src/main.cp3:3:1: error: CP3408: cannot reassign to const 'z'
```

A tolerant fallback also accepts any line carrying both a `CP3xxxx` code and a
`:line:col:` location, in any order.

## Running the harness

```bash
python tests/runner/run_tests.py [--cp3c <path>] [--cases <dir>] \
                                 [--filter <substr>] [--json] [--verbose]
```

| Flag | Meaning |
| --- | --- |
| `--cp3c <path>` | Path to the `cp3c` executable (default: `build/bin/cp3c`). |
| `--cases <dir>` | Cases root (default: `tests/cases`). |
| `--filter <substr>` | Only run cases whose relative path contains `<substr>`. |
| `--json` | Emit a machine-readable JSON summary instead of text. |
| `--verbose` / `-v` | Print a line for every case, not just failures. |
| `--timeout <sec>` | Per-case timeout (default 60). |

Exit codes: **0** when no case failed (all passed, or all skipped because the
compiler is still a scaffold), **1** if any case failed.

### Scaffold safety

While `cp3c` is still a scaffold, the compile commands exit with code `3` and
print the marker `not implemented`. The harness reports such cases as
`skipped (compiler is a scaffold build)` and exits `0` when nothing is runnable
yet - it never crashes on a stub, and it never reports a stub as a failure.

## Adding a case

1. Create `tests/cases/<your-category>/<name>.cp3`.
2. Create `tests/cases/<your-category>/<name>.expect.json` using the schema above.
3. Run `python tests/runner/run_tests.py --filter <your-category> --verbose`.
