# CLI and developer-experience tests

`run_cli_tests.py` is a stdlib-only declarative harness for the command line
surface of `cp3c` and the Windows `cp3.cmd` shim. The language runner
(`tests/runner/run_tests.py`) executes `.cp3` programs; this suite exercises
parsing, help, `init`, `check`, `build`, `run`, `fmt`, `dev`, `clean`,
`install`, `exec`, lifecycle hooks, and the shim itself.

The suite is deterministic and local. It does not use the network, install a
compiler, touch the real user configuration, or install anything into a real
editor. Every case gets a fresh directory under `build/scratch/cli-tests/`.
Successful case directories are deleted after the run; failing ones and
`--keep` runs are retained for inspection.

## Requirements

- Python 3.9 or newer. No third-party packages.
- A built `build/bin/cp3c.exe` (or `build/bin/cp3c` on POSIX).
- On Windows, `packaging/windows/cp3.cmd` is assembled into temporary shim
  layouts once per run. The `cp3c` binary is hardlinked when possible, so the
  shim never copies the full compiler per case.
- Full mode needs a C++ compiler on `PATH` (`g++`, `clang++`, or `cl`). Fast
  mode does not compile.

## Running

```powershell
python tests\cli\run_cli_tests.py --fast
python tests\cli\run_cli_tests.py --full --jobs 4
python tests\cli\run_cli_tests.py --filter init --list
python tests\cli\run_cli_tests.py --filter global,init --json > cli-results.json
```

| Option | Meaning |
| --- | --- |
| `--cp3c PATH` | compiler under test; defaults to `build/bin/cp3c.exe` |
| `--cp3 PATH` | use an existing shim instead of assembling temporary layouts |
| `--filter TEXT` | substring match on `group/name`; repeat or comma-separate |
| `--list` | print selected cases, availability, and skip or slow status |
| `--json` | write a machine-readable result document to stdout |
| `--jobs N` | parallel workers; default 4 |
| `--timeout SECONDS` | default per-case timeout; default 30 |
| `--keep` | keep every case directory, including passing ones |
| `--fast` | skip cases marked `slow`; this is the default |
| `--full` | include `slow` build, link, and run cases |
| `--max-fail N` | stop after N failures; zero means no limit |

Exit status is 0 when there are no failures, 1 when at least one case fails or
an expected failure unexpectedly passes, and 2 for usage or harness errors.
Skips and expected failures do not make the run fail.

## Case manifest

Cases live in `tests/cli/cases/*.json`. A file may contain one case, a list of
cases, or an object with `group` and `cases`. Case identity is
`group/name`; duplicate identities are rejected. The discovery order is stable:
files sorted by path, then manifest order.

A minimal case:

```json
{
  "group": "check",
  "name": "hello-project",
  "description": "check accepts a valid hello project",
  "argv": ["check", "src/index.cp3"],
  "setup": {
    "fixtures": ["hello"]
  },
  "expect": {
    "exit": 0,
    "stdout": {"empty": true}
  }
}
```

Top-level fields:

| Field | Description |
| --- | --- |
| `name` | required; unique within the group |
| `group` | defaults to the manifest or parent directory name |
| `description` | short human-readable summary |
| `tool` | `cp3c` (default) or a shim alias (`cp3`, `cp3-no-vsix`, `cp3-no-cp3c`, `cp3-space`) |
| `argv` | list of strings passed to the tool, without the tool itself |
| `stdin` | string, or `{"text": "..."}` / `{"file": "path"}` |
| `env` | extra environment variables; `PATH_MODE` and `PATH_PREPEND` are special |
| `cwd` | working directory relative to the case work root; default `.` |
| `setup` | files, directories, and fixtures to create before the run |
| `timeout` | per-case timeout in seconds; default 30 |
| `slow` | if true, skipped in fast mode |
| `repeat` | run the command this many times; the streams are concatenated and the exit codes must agree (idempotence checks); default 1 |
| `requires` | list of availability predicates |
| `xfail` | stable id plus one-line reason for a known product defect |
| `expect` | exit code and output/file assertions |

`argv` items are passed directly to the process, not through a shell. This is
intentional: argument splitting bugs in the product remain visible.

## Setup

`setup` accepts:

- `fixtures`: copy a directory from `fixtures/`, or `{"name": "...", "to": "..."}`.
  A fixture without `to` lands in the case work root.
- `home_fixtures`, `config_fixtures`, `localappdata_fixtures`: copy a fixture
  into the isolated home, config, or local application data directory.
- `dirs`: create empty directories.
- `files`: map of relative path to text content. A trailing slash creates a
  directory. Non-string values are written as pretty-printed JSON.
- `chmod`: map of path to an octal string or integer mode.

Setup paths are relative to the work root. Assertion paths under
`expect.files` and `expect.files_absent` are relative to the case `cwd`, which
matches how the product resolves paths after a `cwd` override.

Special fixture names generate deterministic data instead of copying a tree:

- `empty`
- `local-dep.cp3pak`
- `local-dep-bad-version.cp3pak`
- `local-dep-no-package-json.cp3pak`
- `not-a-pak.cp3pak`
- `local-dep-source.cp3pak` (carries the C++ source `cp3 recompile` compiles)
- `local-dep-excluded-source.cp3pak` (excludes the source, ships no cache)
- `local-dep-cached.cp3pak` (carries a cache for this machine's platform)

`.cp3pak` archives use USTAR tar and gzip with mtime zero so repeated runs
produce identical bytes.

## Template tokens and normalization

Paths and `env` values may use these tokens:

`{case}`, `{root}`, `{home}`, `{config}`, `{localappdata}`, `{appdata}`,
`{temp}`, `{programfiles}`, `{programfiles86}`, `{run}`, `{repo}`,
`{fixtures}`, `{platform}`.

`{case}` is the work root. `{root}` is the case root containing work, home,
config, and temporary directories. `{programfiles}` and `{programfiles86}`
point at empty directories so the shim cannot discover a real editor install.
`{platform}` is this machine's canonical platform id (`windows-x64`,
`linux-x64`, `mac-arm64`, ...), spelled the way `cp3c` spells it - it makes
the per-platform archive members assertable without hardcoding a platform.

Before assertions, CRLF and bare CR are converted to LF, ANSI escapes are
removed, and backslashes become `/`. Case and run roots are replaced with the
tokens above so output stays stable across machines and runs. File contents are
normalized the same way for text comparisons.

## Environment isolation

Each case starts with a copy of the developer environment minus the variables
that could leak state: `CP3_ROOT`, `CP3_CONFIG_DIR`, `CP3_TOOLCHAIN`,
`CP3_SCRIPT_GUARD`, `HOME`, `USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, `TEMP`,
`TMP`, `XDG_CONFIG_HOME`, `ProgramFiles`, `ProgramFiles(x86)`, and
`ProgramW6432`. The harness then sets all of those to case-local directories.
`FAKE_EDITOR_LOG` and `FAKE_EDITOR_EXIT` are also cleared.

`env.PATH_MODE = "minimal"` restricts `PATH` to the case `bin` directory plus
the Windows system directories. Use it when a test must not discover a real
editor. `env.PATH_PREPEND` adds one or more directories ahead of the inherited
`PATH`; entries must exist.

## Assertions

`expect` fields:

| Field | Description |
| --- | --- |
| `exit` | exact expected exit status; default 0 |
| `stdout`, `stderr` | string, list of strings, or an object described below |
| `stdout_json_paths`, `stderr_json_paths` | map or list of dotted JSON paths to exact values |
| `files` | map of path to file expectation |
| `files_absent` | list of paths that must not exist |

Stream expectation objects support:

- `contains`: string or list
- `not_contains`: string or list
- `regex`: string or list, applied with `re.MULTILINE`
- `equals`: exact normalized text
- `empty`: true requires only whitespace

File expectation objects support:

- `exists`: true or false; `absent`: true is equivalent to `exists: false`
- `contains`, `not_contains`, `regex`
- `equals`: exact normalized text
- `equals_file`: compare with a golden under `tests/cli/expected/`
- `no_crlf`: fail if the raw bytes still contain CR
- `json_equals`: exact parsed JSON value
- `json_subset`: recursive subset match
- `json_paths`: dotted paths to exact values
- `archive_contains`: member names a `.cp3pak` archive must carry
- `archive_absent`: member names a `.cp3pak` archive must not carry
- `archive_text_contains`: map of archive member name to the text it must contain

An existence-only file expectation returns immediately after confirming the
file is present. The `archive_*` expectations read the file as a `.cp3pak`
(gzip + tar) and check its members.

## Availability and expected failures

`requires` predicates:

| Predicate | Meaning |
| --- | --- |
| `windows` | Windows host |
| `posix` | non-Windows host |
| `compiler` | `g++`, `clang++`, or `cl` on `PATH` |
| `editor` | fake editor fixtures are available (currently always true) |
| `command:NAME` | `NAME` appears in `cp3c help`; `command:cp3` checks the shim |
| `flag:NAME` | `NAME` appears in the `cp3c help` text |

The future `toolchain` and `editor-plugin` cases use `command:toolchain` and
`command:editor-plugin`. Until those commands land they are skipped with an
explicit reason and turn on automatically when `help` lists them. Interactive
wizard cases additionally use `flag:--interactive`; when enabled, they feed
scripted stdin to a non-TTY process. `cp3c init` prompts only on a TTY and has
no `--interactive` switch, so its prompt path is not covered here; non-TTY
defaults and explicit flags are.

`slow: true` marks compile, link, or run cases. They are skipped in fast mode
and included with `--full`.

`xfail` is only for a known product defect:

```json
"xfail": {
  "id": "init-package-object-export",
  "reason": "cp3c init --type package emits an object export that codegen rejects"
}
```

The case still runs. If it fails, it reports `XFAIL`; if it passes, it reports
`XPASS` and the suite exits 1 so the stale expectation gets removed. Keep ids
stable and reasons to one line.

## Shim layouts

On Windows, the harness assembles four read-only layouts once per run:

| Alias | Layout |
| --- | --- |
| `cp3` | `bin/cp3.cmd`, `bin/cp3c.exe`, `share/vscode` VSIX, linked `src` and `third_party` |
| `cp3-no-vsix` | same, without the VSIX |
| `cp3-no-cp3c` | shim only, to test missing-compiler diagnostics |
| `cp3-space` | full layout under `shim with space` |

`--cp3 PATH` bypasses assembly and points every alias at the supplied shim.
The harness never invokes a real editor; the `editor` fixtures use fake `.cmd`
programs that log their argv and honor `FAKE_EDITOR_EXIT`.

## Groups and counts

The suite currently discovers 332 cases:

| Group | Cases |
| --- | ---: |
| `global` | 33 |
| `init` | 34 |
| `check` | 18 |
| `build` | 33 |
| `run` | 17 |
| `fmt` | 12 |
| `dev` | 8 |
| `clean` | 10 |
| `install` | 34 |
| `package` | 17 |
| `recompile` | 5 |
| `exec` | 6 |
| `hooks` | 13 |
| `shim` | 24 |
| `toolchain` | 33 |
| `editor` | 35 |

Fast mode runs everything except `slow` cases and cases whose `requires`
predicate is not met. Full mode adds the compile and link cases.

## Adding a case

1. Pick the group manifest, for example `tests/cli/cases/fmt.json`.
2. Add one object to its `cases` array. Use a unique `name`; behavior names may
   repeat across groups.
3. Point `setup.fixtures` at an existing fixture or add a new directory under
   `tests/cli/fixtures/`.
4. Pass every argument as a separate `argv` item.
5. Assert the exact `exit` first, then the smallest stable output fragment.
   Use `stdout: {"empty": true}` for silent success.
6. Use `xfail` only after confirming a real product defect and give it a stable
   id. Do not weaken an assertion to make a defect disappear.
7. Run the single case with `--filter group/name` and `--list` to confirm it is
   available, then run the full group.

Nothing needs to be registered. Discovery reads every `*.json` under
`tests/cli/cases/`, so a new manifest file is picked up automatically. Golden
files belong in `tests/cli/expected/` and are referenced with `equals_file`.

Fixture recipes such as deterministic `.cp3pak` archives live in
`run_cli_tests.py` (`materialize_fixture`) so the suite creates them on demand
instead of checking in binary archives.
