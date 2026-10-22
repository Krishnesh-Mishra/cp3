#!/usr/bin/env python3
# =============================================================================
# tests/cli/run_cli_tests.py - declarative CLI/UX test harness for cp3c and the
# `cp3` command shim.
#
# The language runner (tests/runner/run_tests.py) executes .cp3 programs; this
# harness exercises the command line itself: parsing, help, init, check, build,
# run, fmt, dev, clean, install, exec, hooks and the Windows `cp3.cmd` shim.
#
# Every case is a JSON object under tests/cli/cases/**/*.json. A file is either
# a single case, a list of cases, or {"group": "...", "cases": [...]}. See
# tests/cli/README.md for the manifest format and a short guide to adding a
# case.
#
# Design rules:
#   * stdlib only (Python 3.9+; developed on 3.14)
#   * one fresh temp root per case under build/scratch/cli-tests/<run>/
#   * isolated HOME/USERPROFILE/LOCALAPPDATA/APPDATA/TEMP/CP3_CONFIG_DIR
#   * CRLF and ANSI normalized before assertions; output paths use "/"
#   * exact exit codes; failing cases print a copy-paste repro command
#   * skipped cases always say why; xfail entries carry a stable id
#   * no network, no real editor installs, no corpus, no benchmarks
#
# Usage:
#   python tests/cli/run_cli_tests.py --fast
#   python tests/cli/run_cli_tests.py --full --jobs 2 --keep
#   python tests/cli/run_cli_tests.py --filter init --list
#   python tests/cli/run_cli_tests.py --filter global,init --json > out.json
#
# Exit codes: 0 no failures, 1 at least one failure (or an XPASS), 2 usage or
# harness error.
# =============================================================================
from __future__ import annotations

import argparse
import gzip
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]
CLI_ROOT = Path(__file__).resolve().parent
CASES_DIR = CLI_ROOT / "cases"
FIXTURES_DIR = CLI_ROOT / "fixtures"
EXPECTED_DIR = CLI_ROOT / "expected"
SCRATCH_ROOT = REPO_ROOT / "build" / "scratch" / "cli-tests"

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
DEFAULT_TIMEOUT = 30.0

EXIT_SUCCESS = 0
EXIT_FAILURE = 1
EXIT_USAGE = 2

# Environment variables a case must never inherit from the developer shell.
ISOLATED_VARIABLES = (
    "CP3_ROOT",
    "CP3_CONFIG_DIR",
    "CP3_TOOLCHAIN",
    "CP3_SCRIPT_GUARD",
    "HOME",
    "USERPROFILE",
    "LOCALAPPDATA",
    "APPDATA",
    "TEMP",
    "TMP",
    "XDG_CONFIG_HOME",
    "ProgramFiles",
    "ProgramFiles(x86)",
    "ProgramW6432",
    "FAKE_EDITOR_LOG",
    "FAKE_EDITOR_EXIT",
)


# -----------------------------------------------------------------------------
# Data model
# -----------------------------------------------------------------------------
@dataclass
class Case:
    name: str
    group: str
    description: str
    argv: list[str]
    stdin: str
    env: dict[str, str]
    cwd: str
    setup: dict[str, Any]
    timeout: float
    slow: bool
    xfail: Optional[dict[str, str]]
    requires: list[str]
    expect: dict[str, Any]
    tool: str
    source: str

    @property
    def display(self) -> str:
        return f"{self.group}/{self.name}"


@dataclass
class CaseDirs:
    root: Path
    work: Path
    home: Path
    config: Path
    localappdata: Path
    appdata: Path
    temp: Path
    bin: Path
    programfiles: Path
    programfiles86: Path


@dataclass
class ProcResult:
    exit_code: Optional[int]
    stdout: str
    stderr: str
    duration_ms: int
    timed_out: bool
    error: Optional[str]


@dataclass
class Result:
    case: Case
    status: str  # PASS, FAIL, SKIP, XFAIL, XPASS
    duration_ms: int = 0
    exit_code: Optional[int] = None
    failures: list[str] = field(default_factory=list)
    skip_reason: str = ""
    stdout: str = ""
    stderr: str = ""
    command: list[str] = field(default_factory=list)
    cwd: str = ""
    case_root: Optional[Path] = None


@dataclass
class ShimSet:
    commands: dict[str, list[str]]
    warnings: list[str] = field(default_factory=list)


# -----------------------------------------------------------------------------
# Normalization and small helpers
# -----------------------------------------------------------------------------
def normalize_text(text: str, dirs: Optional[CaseDirs], run_root: Path) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = ANSI_RE.sub("", text)
    if dirs is not None:
        replacements = [
            (str(dirs.root), "{case}"),
            (str(dirs.home), "{home}"),
            (str(dirs.config), "{config}"),
            (str(dirs.localappdata), "{localappdata}"),
            (str(dirs.appdata), "{appdata}"),
            (str(dirs.temp), "{temp}"),
            (str(dirs.programfiles), "{programfiles}"),
            (str(dirs.programfiles86), "{programfiles86}"),
        ]
        for raw, token in replacements:
            text = text.replace(raw, token)
            text = text.replace(raw.replace("\\", "/"), token)
    text = text.replace(str(run_root), "{run}")
    text = text.replace(str(FIXTURES_DIR), "{fixtures}")
    text = text.replace(str(REPO_ROOT), "{repo}")
    text = text.replace("\\", "/")
    return text


def as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def read_text(path: Path) -> str:
    return path.read_bytes().decode("utf-8", errors="replace")


def json_path(value: Any, dotted: str) -> Any:
    current = value
    for part in dotted.split("."):
        if isinstance(current, list):
            try:
                index = int(part)
            except ValueError:
                raise KeyError(dotted)
            if index < 0 or index >= len(current):
                raise KeyError(dotted)
            current = current[index]
        elif isinstance(current, dict):
            if part not in current:
                raise KeyError(dotted)
            current = current[part]
        else:
            raise KeyError(dotted)
    return current


def json_subset_mismatches(actual: Any, expected: Any, prefix: str = "") -> list[str]:
    problems: list[str] = []
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [f"{prefix or '<root>'}: expected an object"]
        for key, want in expected.items():
            child = f"{prefix}.{key}" if prefix else key
            if key not in actual:
                problems.append(f"{child}: missing")
            else:
                problems.extend(json_subset_mismatches(actual[key], want, child))
        return problems
    if isinstance(expected, list):
        if not isinstance(actual, list):
            return [f"{prefix or '<root>'}: expected an array"]
        if len(actual) != len(expected):
            problems.append(
                f"{prefix or '<root>'}: expected {len(expected)} items, got {len(actual)}"
            )
        for index, want in enumerate(expected[: len(actual)]):
            problems.extend(
                json_subset_mismatches(actual[index], want, f"{prefix}.{index}".strip("."))
            )
        return problems
    if actual != expected:
        problems.append(f"{prefix or '<root>'}: expected {expected!r}, got {actual!r}")
    return problems


def expand_template(value: str, dirs: CaseDirs, run_root: Path) -> str:
    replacements = {
        "{case}": str(dirs.work),
        "{root}": str(dirs.root),
        "{home}": str(dirs.home),
        "{config}": str(dirs.config),
        "{localappdata}": str(dirs.localappdata),
        "{appdata}": str(dirs.appdata),
        "{temp}": str(dirs.temp),
        "{programfiles}": str(dirs.programfiles),
        "{programfiles86}": str(dirs.programfiles86),
        "{run}": str(run_root),
        "{repo}": str(REPO_ROOT),
        "{fixtures}": str(FIXTURES_DIR),
    }
    out = value
    for token, replacement in replacements.items():
        out = out.replace(token, replacement)
    return out


def resolve_setup_path(raw: str, dirs: CaseDirs, run_root: Path) -> Path:
    return resolve_assertion_path(raw, dirs, run_root, dirs.work)


def resolve_assertion_path(
    raw: str, dirs: CaseDirs, run_root: Path, base: Path
) -> Path:
    expanded = expand_template(raw, dirs, run_root)
    path = Path(expanded)
    if not path.is_absolute():
        path = base / path
    return path


def sanitize_dir_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("._")[:80] or "case"


# -----------------------------------------------------------------------------
# Discovery
# -----------------------------------------------------------------------------
def load_case_object(raw: dict[str, Any], group: str, source: str) -> Case:
    name = str(raw.get("name", "")).strip()
    if not name:
        raise ValueError(f"{source}: a case has no name")
    argv_raw = raw.get("argv", [])
    if not isinstance(argv_raw, list) or not all(isinstance(a, str) for a in argv_raw):
        raise ValueError(f"{source}: case '{name}' argv must be a list of strings")
    stdin_raw = raw.get("stdin", "")
    if isinstance(stdin_raw, dict):
        if "text" in stdin_raw:
            stdin = str(stdin_raw["text"])
        elif "file" in stdin_raw:
            stdin = f"@file:{stdin_raw['file']}"
        else:
            raise ValueError(f"{source}: case '{name}' stdin object needs text or file")
    else:
        stdin = str(stdin_raw)
    env_raw = raw.get("env", {})
    if not isinstance(env_raw, dict):
        raise ValueError(f"{source}: case '{name}' env must be an object")
    xfail_raw = raw.get("xfail")
    xfail: Optional[dict[str, str]] = None
    if xfail_raw:
        if isinstance(xfail_raw, str):
            xfail = {"id": xfail_raw, "reason": "known issue"}
        elif isinstance(xfail_raw, dict):
            if not xfail_raw.get("id"):
                raise ValueError(f"{source}: case '{name}' xfail needs an id")
            xfail = {
                "id": str(xfail_raw["id"]),
                "reason": str(xfail_raw.get("reason", "known issue")),
            }
        else:
            raise ValueError(f"{source}: case '{name}' xfail must be a string or object")
    requires_raw = raw.get("requires", [])
    if isinstance(requires_raw, str):
        requires = [requires_raw]
    elif isinstance(requires_raw, list):
        requires = [str(item) for item in requires_raw]
    else:
        raise ValueError(f"{source}: case '{name}' requires must be a string or list")
    expect = raw.get("expect", {})
    if not isinstance(expect, dict):
        raise ValueError(f"{source}: case '{name}' expect must be an object")
    setup = raw.get("setup", {})
    if not isinstance(setup, dict):
        raise ValueError(f"{source}: case '{name}' setup must be an object")
    timeout = float(raw.get("timeout", DEFAULT_TIMEOUT))
    if timeout <= 0:
        raise ValueError(f"{source}: case '{name}' timeout must be positive")
    return Case(
        name=name,
        group=str(raw.get("group", group)),
        description=str(raw.get("description", "")),
        argv=argv_raw,
        stdin=stdin,
        env={str(k): str(v) for k, v in env_raw.items()},
        cwd=str(raw.get("cwd", ".")),
        setup=setup,
        timeout=timeout,
        slow=bool(raw.get("slow", False)),
        xfail=xfail,
        requires=requires,
        expect=expect,
        tool=str(raw.get("tool", "cp3c")),
        source=source,
    )


def discover_cases() -> list[Case]:
    if not CASES_DIR.is_dir():
        raise SystemExit(f"cases directory not found: {CASES_DIR}")
    cases: list[Case] = []
    for path in sorted(CASES_DIR.rglob("*.json")):
        rel = path.relative_to(CASES_DIR).as_posix()
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise SystemExit(f"{rel}: invalid JSON ({exc})") from exc
        if isinstance(doc, list):
            entries = doc
            file_group = path.parent.name
        elif isinstance(doc, dict) and "cases" in doc:
            entries = doc["cases"]
            file_group = str(doc.get("group", path.parent.name))
            if not isinstance(entries, list):
                raise SystemExit(f"{rel}: 'cases' must be a list")
        elif isinstance(doc, dict):
            entries = [doc]
            file_group = path.parent.name
        else:
            raise SystemExit(f"{rel}: a case file must be an object or a list")
        for index, raw in enumerate(entries):
            if not isinstance(raw, dict):
                raise SystemExit(f"{rel}: case #{index + 1} is not an object")
            cases.append(load_case_object(raw, file_group, f"{rel}#{index + 1}"))
    names: set[str] = set()
    for case in cases:
        if case.display in names:
            raise SystemExit(f"duplicate case name: {case.display}")
        names.add(case.display)
    return cases


# -----------------------------------------------------------------------------
# Fixtures
# -----------------------------------------------------------------------------
def build_cp3pak(
    destination: Path,
    *,
    format_version: int = 1,
    include_package_json: bool = True,
) -> None:
    """Write a deterministic .cp3pak from the local-dep fixture."""
    source = FIXTURES_DIR / "local-dep"
    package_json = (source / "package.json").read_text(encoding="utf-8")
    entry = (source / "src" / "index.cp3").read_text(encoding="utf-8")
    readme = (source / "README.md").read_text(encoding="utf-8")
    manifest = {
        "formatVersion": format_version,
        "name": "local-kit",
        "version": "1.2.3",
        "main": "src/index.cp3",
    }
    members: list[tuple[str, bytes]] = [
        ("cp3pak.json", (json.dumps(manifest, indent=2) + "\n").encode("utf-8")),
    ]
    if include_package_json:
        members.append(("package.json", package_json.encode("utf-8")))
    members.append(("index.cp3", entry.encode("utf-8")))
    members.append(("README.md", readme.encode("utf-8")))

    tar_buffer = io.BytesIO()
    with tarfile.open(fileobj=tar_buffer, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for member_name, data in members:
            info = tarfile.TarInfo(member_name)
            info.size = len(data)
            info.mtime = 0
            info.mode = 0o644
            info.uid = 0
            info.gid = 0
            archive.addfile(info, io.BytesIO(data))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
            compressed.write(tar_buffer.getvalue())


def materialize_fixture(name: str, destination: Path) -> None:
    if name == "empty":
        destination.mkdir(parents=True, exist_ok=True)
        return
    if name == "local-dep.cp3pak":
        build_cp3pak(destination)
        return
    if name == "local-dep-bad-version.cp3pak":
        build_cp3pak(destination, format_version=99)
        return
    if name == "local-dep-no-package-json.cp3pak":
        build_cp3pak(destination, include_package_json=False)
        return
    if name == "not-a-pak.cp3pak":
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("this is not a gzip stream\n", encoding="utf-8")
        return
    source = FIXTURES_DIR / name
    if not source.is_dir():
        raise FileNotFoundError(f"unknown fixture '{name}'")
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, dirs_exist_ok=True, copy_function=shutil.copy2)


def apply_setup(case: Case, dirs: CaseDirs, run_root: Path) -> None:
    setup = case.setup
    for item in as_list(setup.get("fixtures")):
        if isinstance(item, str):
            materialize_fixture(item, dirs.work)
        elif isinstance(item, dict):
            name = str(item.get("name", ""))
            if not name:
                raise ValueError(f"{case.display}: fixture entry needs a name")
            target = resolve_setup_path(str(item.get("to", ".")), dirs, run_root)
            if name.endswith(".cp3pak") or name == "empty":
                materialize_fixture(name, target)
            else:
                materialize_fixture(name, target)
        else:
            raise ValueError(f"{case.display}: bad fixture entry {item!r}")

    for item in as_list(setup.get("home_fixtures")):
        materialize_fixture(str(item), dirs.home)
    for item in as_list(setup.get("config_fixtures")):
        materialize_fixture(str(item), dirs.config)
    for item in as_list(setup.get("localappdata_fixtures")):
        materialize_fixture(str(item), dirs.localappdata)

    for raw_dir in as_list(setup.get("dirs")):
        resolve_setup_path(str(raw_dir), dirs, run_root).mkdir(parents=True, exist_ok=True)

    files = setup.get("files", {})
    if files and not isinstance(files, dict):
        raise ValueError(f"{case.display}: setup.files must be an object")
    for raw_path, content in files.items():
        target = resolve_setup_path(str(raw_path), dirs, run_root)
        if str(raw_path).endswith(("/", "\\")):
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if not isinstance(content, str):
            content = json.dumps(content, indent=2) + "\n"
        target.write_text(content, encoding="utf-8", newline="")

    chmod_map = setup.get("chmod", {})
    if chmod_map:
        for raw_path, mode in chmod_map.items():
            target = resolve_setup_path(str(raw_path), dirs, run_root)
            if isinstance(mode, str):
                mode = int(mode, 8)
            os.chmod(target, int(mode))


# -----------------------------------------------------------------------------
# Shim layout
# -----------------------------------------------------------------------------
def make_junction(link: Path, target: Path, warnings: list[str]) -> None:
    if link.exists():
        return
    result = subprocess.run(
        ["cmd", "/d", "/c", "mklink", "/J", str(link), str(target)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode == 0:
        return
    try:
        link.symlink_to(target, target_is_directory=True)
        return
    except OSError:
        warnings.append(
            f"could not link {link.name} to the source tree; shim builds may "
            "rely on cp3c's compile-time root discovery"
        )


def link_or_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


def assemble_shim(
    root: Path,
    cp3c: Path,
    *,
    with_cp3c: bool,
    with_vsix: bool,
    with_trees: bool,
    warnings: list[str],
) -> None:
    bin_dir = root / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO_ROOT / "packaging" / "windows" / "cp3.cmd", bin_dir / "cp3.cmd")
    if with_cp3c:
        link_or_copy(cp3c, bin_dir / "cp3c.exe")
    if with_vsix:
        vsix_dir = root / "share" / "vscode"
        vsix_dir.mkdir(parents=True, exist_ok=True)
        (vsix_dir / "cp3-vscode-0.1.0.vsix").write_text(
            "fake vsix for the CLI shim tests\n", encoding="utf-8"
        )
    if with_trees:
        make_junction(root / "src", REPO_ROOT / "src", warnings)
        make_junction(root / "third_party", REPO_ROOT / "third_party", warnings)


def build_shims(run_root: Path, cp3c: Path, provided_cp3: Optional[Path]) -> ShimSet:
    warnings: list[str] = []
    comspec = os.environ.get("ComSpec", "cmd.exe")
    commands: dict[str, list[str]] = {}
    if provided_cp3 is not None:
        for alias in ("cp3", "cp3-no-vsix", "cp3-no-cp3c", "cp3-space"):
            commands[alias] = [comspec, "/d", "/c", str(provided_cp3)]
        return ShimSet(commands, warnings)

    variants = {
        "cp3": (run_root / "_shim", True, True, True),
        "cp3-no-vsix": (run_root / "_shim-no-vsix", True, False, True),
        "cp3-no-cp3c": (run_root / "_shim-no-cp3c", False, False, False),
        "cp3-space": (run_root / "shim with space", True, True, True),
    }
    for alias, (root, with_cp3c, with_vsix, with_trees) in variants.items():
        assemble_shim(
            root,
            cp3c,
            with_cp3c=with_cp3c,
            with_vsix=with_vsix,
            with_trees=with_trees,
            warnings=warnings,
        )
        commands[alias] = [comspec, "/d", "/c", str(root / "bin" / "cp3.cmd")]
    return ShimSet(commands, warnings)


# -----------------------------------------------------------------------------
# Availability probes
# -----------------------------------------------------------------------------
class Probe:
    def __init__(self, cp3c: Path, cp3_available: bool) -> None:
        self.cp3c = cp3c
        self.cp3_available = cp3_available
        self.help_text = ""
        self.commands: set[str] = set()
        self.flags: set[str] = set()
        self.compiler: Optional[str] = None
        self.clang: Optional[str] = None
        self._probe_help()
        self._probe_compiler()

    def _probe_help(self) -> None:
        if not self.cp3c.exists():
            return
        try:
            result = subprocess.run(
                [str(self.cp3c), "help"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=15,
            )
        except (OSError, subprocess.SubprocessError):
            return
        self.help_text = result.stdout + result.stderr
        in_commands = False
        for line in self.help_text.splitlines():
            if line.strip() == "Commands:":
                in_commands = True
                continue
            if in_commands:
                match = re.match(r"^\s{2,}([a-z][a-z0-9-]*)\b", line)
                if match:
                    self.commands.add(match.group(1))
                elif line.strip() and not line.startswith(" "):
                    in_commands = False
        self.flags = set(re.findall(r"--[a-z][a-z0-9-]*", self.help_text))

    def _probe_compiler(self) -> None:
        for name in ("g++", "clang++", "cl"):
            found = shutil.which(name)
            if found:
                self.compiler = found
                break
        self.clang = shutil.which("clang++") or self._clang_in_standard_location()

    @staticmethod
    def _clang_in_standard_location() -> Optional[str]:
        """Clang in the standard install directory, which is not on PATH.

        cp3c looks in the standard system locations as well as PATH, so a
        Windows LLVM install that was never added to PATH still counts as
        installed. The probe must agree with the compiler, or the case would
        skip on a machine where Clang in fact works.
        """
        if os.name != "nt":
            return None
        for root in (
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM",
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
            / "LLVM",
        ):
            candidate = root / "bin" / "clang++.exe"
            if candidate.is_file():
                return str(candidate)
        return None

    def check(self, predicate: str) -> tuple[bool, str]:
        if predicate == "windows":
            return (os.name == "nt", "requires Windows")
        if predicate == "posix":
            return (os.name != "nt", "requires POSIX")
        if predicate == "compiler":
            return (
                self.compiler is not None,
                "requires a C++ compiler (g++, clang++ or cl) on PATH",
            )
        if predicate == "clang":
            return (self.clang is not None, "requires clang++ on PATH")
        if predicate == "editor":
            return (True, "")
        if predicate.startswith("command:"):
            name = predicate.split(":", 1)[1]
            if name == "cp3":
                return (self.cp3_available, "requires the cp3 shim")
            return (
                name in self.commands,
                f"requires command '{name}' (not listed in cp3c help)",
            )
        if predicate.startswith("flag:"):
            name = predicate.split(":", 1)[1]
            return (
                name in self.flags,
                f"requires flag '{name}' (not listed in cp3c help)",
            )
        return (False, f"unknown requires predicate '{predicate}'")


def case_skip_reason(case: Case, probe: Probe, full: bool) -> str:
    for predicate in case.requires:
        ok, reason = probe.check(predicate)
        if not ok:
            return reason
    if case.slow and not full:
        return "slow case (use --full)"
    return ""


# -----------------------------------------------------------------------------
# Execution
# -----------------------------------------------------------------------------
def make_dirs(run_root: Path, index: int, case: Case) -> CaseDirs:
    root = run_root / f"{index:03d}-{sanitize_dir_name(case.name)}"
    dirs = CaseDirs(
        root=root,
        work=root / "work",
        home=root / "home",
        config=root / "config",
        localappdata=root / "localappdata",
        appdata=root / "appdata",
        temp=root / "temp",
        bin=root / "bin",
        programfiles=root / "program-files",
        programfiles86=root / "program-files-x86",
    )
    for directory in (
        dirs.work,
        dirs.home,
        dirs.config,
        dirs.localappdata,
        dirs.appdata,
        dirs.temp,
        dirs.bin,
        dirs.programfiles,
        dirs.programfiles86,
    ):
        directory.mkdir(parents=True, exist_ok=True)
    # cp3c looks for the bundled VSIX in ../share/vscode relative to its own
    # executable. Direct-compiler cases need that layout too, not just the
    # assembled shim variants.
    vsix_dir = root / "share" / "vscode"
    vsix_dir.mkdir(parents=True, exist_ok=True)
    (vsix_dir / "cp3-vscode-0.1.0.vsix").write_text(
        "fake vsix for the CLI tests\n", encoding="utf-8"
    )
    return dirs


def build_environment(
    case: Case, dirs: CaseDirs, run_root: Path, comspec: str
) -> dict[str, str]:
    env = dict(os.environ)
    for key in ISOLATED_VARIABLES:
        env.pop(key, None)
    env["HOME"] = str(dirs.home)
    env["USERPROFILE"] = str(dirs.home)
    env["LOCALAPPDATA"] = str(dirs.localappdata)
    env["APPDATA"] = str(dirs.appdata)
    env["TEMP"] = str(dirs.temp)
    env["TMP"] = str(dirs.temp)
    env["XDG_CONFIG_HOME"] = str(dirs.config)
    env["CP3_CONFIG_DIR"] = str(dirs.config)
    env["ProgramFiles"] = str(dirs.programfiles)
    env["ProgramFiles(x86)"] = str(dirs.programfiles86)
    env["ProgramW6432"] = str(dirs.programfiles)
    env["FAKE_EDITOR_LOG"] = str(dirs.root / "editor.log")
    env["ComSpec"] = comspec
    # Direct cp3c editor cases use the real compiler binary, so its relative
    # repository search would never see the per-case fake VSIX. Point CP3_ROOT
    # at the case root for those cases only. The cp3-no-vsix shim variant must
    # stay without a package so its failure path is exercised.
    if case.group == "editor" and case.tool == "cp3c":
        env["CP3_ROOT"] = str(dirs.root)

    base_path = os.environ.get("PATH", "")
    path_mode = case.env.get("PATH_MODE", "")
    path_entries = [str(dirs.bin)]
    path_spec = case.env.get("PATH_PREPEND", "")
    if path_spec:
        for entry in re.split(r"[;,]", path_spec):
            entry = entry.strip()
            if not entry:
                continue
            expanded = expand_template(entry, dirs, run_root)
            candidate = Path(expanded)
            if not candidate.is_absolute():
                candidate = FIXTURES_DIR / candidate
            if not candidate.exists():
                raise FileNotFoundError(f"{case.display}: PATH entry missing: {candidate}")
            path_entries.append(str(candidate))
    if path_mode != "minimal":
        if base_path:
            path_entries.append(base_path)
    else:
        system_root = os.environ.get("SystemRoot", r"C:\Windows")
        path_entries.extend(
            [str(Path(system_root) / "System32"), system_root, str(Path(system_root) / "System32" / "Wbem")]
        )
        # The Windows cp3c build links the MinGW runtime dynamically. A
        # reduced PATH must still contain that runtime, or cp3c fails to start
        # with STATUS_DLL_NOT_FOUND before the command under test runs.
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler:
            path_entries.append(str(Path(compiler).resolve().parent))
    env["PATH"] = os.pathsep.join(part for part in path_entries if part)

    for key, value in case.env.items():
        if key in ("PATH_MODE", "PATH_PREPEND"):
            continue
        env[key] = expand_template(value, dirs, run_root)
    return env


def resolve_stdin(case: Case, dirs: CaseDirs, run_root: Path) -> bytes:
    text = case.stdin
    if text.startswith("@file:"):
        raw = text[len("@file:") :]
        path = resolve_setup_path(raw, dirs, run_root)
        return path.read_bytes()
    return expand_template(text, dirs, run_root).encode("utf-8")


def kill_process_tree(process: subprocess.Popen[bytes]) -> None:
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(process.pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    else:
        try:
            os.killpg(os.getpgid(process.pid), 9)
        except OSError:
            process.kill()


def execute(command: list[str], cwd: Path, env: dict[str, str], stdin_bytes: bytes, timeout: float) -> ProcResult:
    start = time.perf_counter()
    kwargs: dict[str, Any] = {
        "cwd": str(cwd),
        "env": env,
        "stdin": subprocess.PIPE,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
    }
    if os.name == "nt":
        kwargs["creationflags"] = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    else:
        kwargs["start_new_session"] = True
    try:
        process = subprocess.Popen(command, **kwargs)
    except OSError as exc:
        return ProcResult(None, "", "", int((time.perf_counter() - start) * 1000), False, str(exc))
    timed_out = False
    try:
        stdout_bytes, stderr_bytes = process.communicate(stdin_bytes, timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_process_tree(process)
        try:
            stdout_bytes, stderr_bytes = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            stdout_bytes, stderr_bytes = b"", b""
    duration_ms = int((time.perf_counter() - start) * 1000)
    return ProcResult(
        process.returncode,
        stdout_bytes.decode("utf-8", errors="replace"),
        stderr_bytes.decode("utf-8", errors="replace"),
        duration_ms,
        timed_out,
        None,
    )


# -----------------------------------------------------------------------------
# Assertions
# -----------------------------------------------------------------------------
def check_stream(name: str, text: str, expectation: Any, failures: list[str]) -> None:
    if expectation is None:
        return
    if isinstance(expectation, str):
        if expectation not in text:
            failures.append(f"{name} does not contain {expectation!r}")
        return
    if isinstance(expectation, list):
        for item in expectation:
            if str(item) not in text:
                failures.append(f"{name} does not contain {item!r}")
        return
    if not isinstance(expectation, dict):
        failures.append(f"{name}: unsupported expectation {expectation!r}")
        return
    for needle in as_list(expectation.get("contains")):
        if str(needle) not in text:
            failures.append(f"{name} does not contain {needle!r}")
    for needle in as_list(expectation.get("not_contains")):
        if str(needle) in text:
            failures.append(f"{name} unexpectedly contains {needle!r}")
    for pattern in as_list(expectation.get("regex")):
        if re.search(str(pattern), text, re.MULTILINE) is None:
            failures.append(f"{name} does not match /{pattern}/")
    if "equals" in expectation and text != str(expectation["equals"]):
        failures.append(f"{name} is not exactly {expectation['equals']!r}")
    if expectation.get("empty") and text.strip():
        failures.append(f"{name} is not empty")


def check_json_stream(
    name: str, text: str, expectations: Any, failures: list[str]
) -> None:
    if not expectations:
        return
    try:
        document = json.loads(text)
    except json.JSONDecodeError as exc:
        failures.append(f"{name} is not valid JSON ({exc})")
        return
    if isinstance(expectations, dict):
        items = expectations.items()
    else:
        items = [(entry["path"], entry.get("equals")) for entry in expectations]
    for dotted, expected in items:
        try:
            actual = json_path(document, str(dotted))
        except KeyError:
            failures.append(f"{name} JSON path '{dotted}' is missing")
            continue
        if actual != expected:
            failures.append(
                f"{name} JSON path '{dotted}': expected {expected!r}, got {actual!r}"
            )


def check_file(name: str, path: Path, expectation: Any, failures: list[str]) -> None:
    if isinstance(expectation, str):
        expectation = {"contains": expectation}
    elif isinstance(expectation, list):
        expectation = {"contains": expectation}
    if not isinstance(expectation, dict):
        failures.append(f"file {name}: unsupported expectation {expectation!r}")
        return
    exists = path.exists()
    wants_absent = expectation.get("absent") is True or expectation.get("exists") is False
    if wants_absent:
        if exists:
            failures.append(f"file {name} exists but should not")
        return
    if not exists:
        failures.append(f"file {name} is missing")
        return
    if not path.is_file():
        failures.append(f"file {name} is not a regular file")
        return
    if expectation.get("exists") is True and len(expectation) == 1:
        return
    raw_text = read_text(path)
    if expectation.get("no_crlf") and "\r" in raw_text:
        failures.append(f"file {name} still contains CRLF or CR bytes")
    text = raw_text.replace("\r\n", "\n").replace("\r", "\n")
    for needle in as_list(expectation.get("contains")):
        if str(needle) not in text:
            failures.append(f"file {name} does not contain {needle!r}")
    for needle in as_list(expectation.get("not_contains")):
        if str(needle) in text:
            failures.append(f"file {name} unexpectedly contains {needle!r}")
    for pattern in as_list(expectation.get("regex")):
        if re.search(str(pattern), text, re.MULTILINE) is None:
            failures.append(f"file {name} does not match /{pattern}/")
    if "equals" in expectation and text != str(expectation["equals"]):
        failures.append(f"file {name} is not exactly the expected text")
    if "equals_file" in expectation:
        expected_path = EXPECTED_DIR / str(expectation["equals_file"])
        if not expected_path.is_file():
            failures.append(f"file {name}: expected file {expected_path} is missing")
        else:
            expected_text = read_text(expected_path).replace("\r\n", "\n").replace("\r", "\n")
            if text != expected_text:
                failures.append(f"file {name} does not equal expected/{expectation['equals_file']}")
    if "json_equals" in expectation or "json_subset" in expectation or "json_paths" in expectation:
        try:
            document = json.loads(text)
        except json.JSONDecodeError as exc:
            failures.append(f"file {name} is not valid JSON ({exc})")
            return
        if "json_equals" in expectation and document != expectation["json_equals"]:
            failures.append(f"file {name} JSON is not exactly the expected value")
        if "json_subset" in expectation:
            for problem in json_subset_mismatches(document, expectation["json_subset"]):
                failures.append(f"file {name} JSON {problem}")
        for dotted, expected in (expectation.get("json_paths") or {}).items():
            try:
                actual = json_path(document, str(dotted))
            except KeyError:
                failures.append(f"file {name} JSON path '{dotted}' is missing")
                continue
            if actual != expected:
                failures.append(
                    f"file {name} JSON path '{dotted}': expected {expected!r}, got {actual!r}"
                )


def check_expectations(
    case: Case,
    process: ProcResult,
    dirs: CaseDirs,
    run_root: Path,
) -> list[str]:
    expect = case.expect
    failures: list[str] = []
    expected_exit = expect.get("exit", 0)
    if process.timed_out:
        failures.append(f"timed out after {case.timeout:g}s")
    if process.error:
        failures.append(f"could not start the command: {process.error}")
    elif process.exit_code != expected_exit:
        failures.append(f"exit code: expected {expected_exit}, got {process.exit_code}")
    stdout = normalize_text(process.stdout, dirs, run_root)
    stderr = normalize_text(process.stderr, dirs, run_root)
    check_stream("stdout", stdout, expect.get("stdout"), failures)
    check_stream("stderr", stderr, expect.get("stderr"), failures)
    check_json_stream("stdout", stdout, expect.get("stdout_json_paths"), failures)
    check_json_stream("stderr", stderr, expect.get("stderr_json_paths"), failures)

    assertion_cwd = dirs.work
    if case.cwd and case.cwd != ".":
        assertion_cwd = (dirs.work / case.cwd).resolve()
    for raw_path, expectation in (expect.get("files") or {}).items():
        path = resolve_assertion_path(str(raw_path), dirs, run_root, assertion_cwd)
        check_file(str(raw_path), path, expectation, failures)
    for raw_path in as_list(expect.get("files_absent")):
        path = resolve_assertion_path(str(raw_path), dirs, run_root, assertion_cwd)
        check_file(str(raw_path), path, {"absent": True}, failures)
    return failures


# -----------------------------------------------------------------------------
# Running a case
# -----------------------------------------------------------------------------
def tool_command(case: Case, shims: ShimSet, probe: Probe, dirs: CaseDirs, run_root: Path) -> list[str]:
    if case.tool == "cp3c":
        return [str(probe.cp3c)]
    if case.tool in shims.commands:
        return list(shims.commands[case.tool])
    raise ValueError(f"{case.display}: unknown tool '{case.tool}'")


def repro_command(command: list[str], cwd: Path) -> str:
    return f"cd {cwd}\n" + subprocess.list2cmdline(command)


def run_case(
    index: int,
    case: Case,
    probe: Probe,
    shims: ShimSet,
    run_root: Path,
    comspec: str,
) -> Result:
    dirs = make_dirs(run_root, index, case)
    base = Result(case=case, status="FAIL", case_root=dirs.root, cwd=str(dirs.work))
    try:
        apply_setup(case, dirs, run_root)
        env = build_environment(case, dirs, run_root, comspec)
        stdin_bytes = resolve_stdin(case, dirs, run_root)
        command = tool_command(case, shims, probe, dirs, run_root)
        argv = [expand_template(arg, dirs, run_root) for arg in case.argv]
        command.extend(argv)
        cwd = dirs.work
        if case.cwd and case.cwd != ".":
            cwd = (dirs.work / case.cwd).resolve()
            if dirs.work.resolve() not in cwd.parents and cwd != dirs.work.resolve():
                raise ValueError(f"{case.display}: cwd escapes the case directory")
        if not cwd.is_dir():
            raise FileNotFoundError(f"{case.display}: cwd not found: {cwd}")
    except Exception as exc:  # noqa: BLE001 - a setup problem is a case failure
        base.failures = [f"setup failed: {exc}"]
        return base

    base.command = command
    base.cwd = str(cwd)
    process = execute(command, cwd, env, stdin_bytes, case.timeout)
    base.exit_code = process.exit_code
    base.stdout = normalize_text(process.stdout, dirs, run_root)
    base.stderr = normalize_text(process.stderr, dirs, run_root)
    base.duration_ms = process.duration_ms
    failures = check_expectations(case, process, dirs, run_root)
    base.failures = failures
    if failures:
        if case.xfail:
            base.status = "XFAIL"
        else:
            base.status = "FAIL"
    else:
        base.status = "XPASS" if case.xfail else "PASS"
    return base


def skip_result(index: int, case: Case, reason: str) -> Result:
    return Result(case=case, status="SKIP", skip_reason=reason)


# -----------------------------------------------------------------------------
# Reporting
# -----------------------------------------------------------------------------
def status_line(result: Result, full: bool) -> str:
    ms = result.duration_ms
    suffix = f" ({ms} ms)" if ms else ""
    return f"{result.status:<5} {result.case.display}{suffix}"


def print_result_detail(result: Result) -> None:
    if result.status == "SKIP":
        print(f"      skipped: {result.skip_reason}")
        return
    if result.status == "XFAIL":
        print(f"      xfail: {result.case.xfail['id']} - {result.case.xfail['reason']}")
    if result.status == "XPASS":
        print(
            f"      xpass: xfail '{result.case.xfail['id']}' now passes; "
            "remove the marker"
        )
    if result.status in ("FAIL", "XPASS"):
        for failure in result.failures:
            print(f"      {failure}")
        if result.stderr.strip():
            print("      stderr:")
            for line in result.stderr.strip().splitlines()[:30]:
                print(f"        {line}")
        elif result.stdout.strip():
            print("      stdout:")
            for line in result.stdout.strip().splitlines()[:30]:
                print(f"        {line}")
        if result.command:
            print("      repro:")
            for line in repro_command(result.command, Path(result.cwd)).splitlines():
                print(f"        {line}")


def print_summary(results: list[Result]) -> None:
    groups: dict[str, dict[str, int]] = {}
    for result in results:
        counts = groups.setdefault(
            result.case.group,
            {"total": 0, "pass": 0, "fail": 0, "skip": 0, "xfail": 0, "xpass": 0},
        )
        counts["total"] += 1
        key = {
            "PASS": "pass",
            "FAIL": "fail",
            "SKIP": "skip",
            "XFAIL": "xfail",
            "XPASS": "xpass",
        }[result.status]
        counts[key] += 1
    print()
    print("Summary by group")
    print(f"{'group':<14} {'total':>6} {'pass':>6} {'fail':>6} {'skip':>6} {'xfail':>6} {'xpass':>6}")
    for group in sorted(groups):
        counts = groups[group]
        print(
            f"{group:<14} {counts['total']:>6} {counts['pass']:>6} "
            f"{counts['fail']:>6} {counts['skip']:>6} {counts['xfail']:>6} "
            f"{counts['xpass']:>6}"
        )
    totals = {key: sum(value[key] for value in groups.values()) for key in ("total", "pass", "fail", "skip", "xfail", "xpass")}
    print(
        f"{'TOTAL':<14} {totals['total']:>6} {totals['pass']:>6} "
        f"{totals['fail']:>6} {totals['skip']:>6} {totals['xfail']:>6} "
        f"{totals['xpass']:>6}"
    )


def results_to_json(
    results: list[Result],
    *,
    run_root: Path,
    probe: Probe,
    shims: ShimSet,
    full: bool,
    jobs: int,
    total_ms: int,
) -> dict[str, Any]:
    counts = {"total": 0, "pass": 0, "fail": 0, "skip": 0, "xfail": 0, "xpass": 0}
    by_group: dict[str, dict[str, int]] = {}
    payload_cases: list[dict[str, Any]] = []
    for result in results:
        counts["total"] += 1
        key = {
            "PASS": "pass",
            "FAIL": "fail",
            "SKIP": "skip",
            "XFAIL": "xfail",
            "XPASS": "xpass",
        }[result.status]
        counts[key] += 1
        group = by_group.setdefault(
            result.case.group,
            {"total": 0, "pass": 0, "fail": 0, "skip": 0, "xfail": 0, "xpass": 0},
        )
        group["total"] += 1
        group[key] += 1
        payload_cases.append(
            {
                "name": result.case.name,
                "group": result.case.group,
                "description": result.case.description,
                "status": result.status,
                "durationMs": result.duration_ms,
                "exitCode": result.exit_code,
                "requires": result.case.requires,
                "slow": result.case.slow,
                "skipReason": result.skip_reason,
                "xfail": result.case.xfail,
                "failures": result.failures,
                "command": result.command,
                "cwd": result.cwd,
                "stdout": result.stdout[:20000],
                "stderr": result.stderr[:20000],
            }
        )
    return {
        "tool": {"cp3c": str(probe.cp3c), "compiler": probe.compiler},
        "run": {
            "dir": str(run_root),
            "mode": "full" if full else "fast",
            "jobs": jobs,
            "durationMs": total_ms,
            "shimWarnings": shims.warnings,
        },
        "cases": payload_cases,
        "summary": {
            **counts,
            "byGroup": by_group,
            "durationMs": total_ms,
        },
    }


# -----------------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------------
def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="run_cli_tests.py",
        description="Declarative CLI test harness for cp3c and the cp3 shim.",
    )
    parser.add_argument("--cp3c", default=None, help="path to cp3c(.exe)")
    parser.add_argument("--cp3", default=None, help="path to a cp3 shim (default: assembled temp layout)")
    parser.add_argument("--filter", action="append", default=[], help="substring filter, repeatable or comma-separated")
    parser.add_argument("--list", action="store_true", help="list selected cases and exit")
    parser.add_argument("--json", action="store_true", help="machine-readable output on stdout")
    parser.add_argument("--jobs", type=int, default=4, help="parallel case workers (default 4)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="default per-case timeout in seconds")
    parser.add_argument("--keep", action="store_true", help="keep every temp case directory")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--fast", action="store_true", help="skip slow cases (default)")
    mode.add_argument("--full", action="store_true", help="include slow build/link cases")
    parser.add_argument("--max-fail", type=int, default=0, help="stop after N failures (0 = no limit)")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.max_fail < 0:
        parser.error("--max-fail must be 0 or greater")
    return args


def default_cp3c() -> Path:
    name = "cp3c.exe" if os.name == "nt" else "cp3c"
    return REPO_ROOT / "build" / "bin" / name


def select_cases(cases: list[Case], filters: list[str]) -> list[Case]:
    needles: list[str] = []
    for entry in filters:
        needles.extend(part.strip().lower() for part in entry.split(",") if part.strip())
    if not needles:
        return cases
    selected = []
    for case in cases:
        haystack = f"{case.group}/{case.name}".lower()
        if any(needle in haystack for needle in needles):
            selected.append(case)
    return selected


def print_list(selected: list[Case], probe: Probe, full: bool) -> None:
    for case in selected:
        reason = case_skip_reason(case, probe, full)
        marker = "available" if not reason else f"skip: {reason}"
        slow = " slow" if case.slow else ""
        print(f"{case.group:<12} {case.name:<44} {marker}{slow}")


def make_run_root() -> Path:
    SCRATCH_ROOT.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    candidate = SCRATCH_ROOT / f"{stamp}-{os.getpid()}"
    counter = 1
    while candidate.exists():
        counter += 1
        candidate = SCRATCH_ROOT / f"{stamp}-{os.getpid()}-{counter}"
    candidate.mkdir(parents=True)
    return candidate


def cleanup_run(run_root: Path, results: list[Result], keep: bool) -> None:
    if keep:
        return
    for result in results:
        if result.case_root is None:
            continue
        if result.status in ("FAIL", "XPASS"):
            continue
        shutil.rmtree(result.case_root, ignore_errors=True)
    if any(result.status in ("FAIL", "XPASS") for result in results):
        return
    shutil.rmtree(run_root, ignore_errors=True)


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    full = bool(args.full)
    cp3c = Path(args.cp3c).resolve() if args.cp3c else default_cp3c()
    provided_cp3 = Path(args.cp3).resolve() if args.cp3 else None

    cases = discover_cases()
    selected = select_cases(cases, args.filter)
    cp3_available = provided_cp3 is not None or os.name == "nt"
    probe = Probe(cp3c, cp3_available)

    if args.list:
        if args.json:
            payload = []
            for case in selected:
                reason = case_skip_reason(case, probe, full)
                payload.append(
                    {
                        "name": case.name,
                        "group": case.group,
                        "slow": case.slow,
                        "requires": case.requires,
                        "available": not reason,
                        "skipReason": reason,
                    }
                )
            print(json.dumps({"cases": payload, "total": len(payload)}, indent=2))
        else:
            print_list(selected, probe, full)
        return EXIT_SUCCESS

    if not cp3c.exists():
        print(f"error: cp3c not found at {cp3c}", file=sys.stderr)
        print("build it first, or pass --cp3c PATH", file=sys.stderr)
        return EXIT_USAGE

    run_root = make_run_root()
    shims = build_shims(run_root, cp3c, provided_cp3)
    comspec = os.environ.get("ComSpec", "cmd.exe")

    start = time.perf_counter()
    results: list[Optional[Result]] = [None] * len(selected)
    failures_seen = 0
    stop = False
    pending: dict[Any, int] = {}
    next_index = 0

    def worker(index: int, case: Case) -> Result:
        return run_case(index, case, probe, shims, run_root, comspec)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while next_index < len(selected) or pending:
            while not stop and next_index < len(selected) and len(pending) < args.jobs:
                case = selected[next_index]
                reason = case_skip_reason(case, probe, full)
                if reason:
                    results[next_index] = skip_result(next_index, case, reason)
                    next_index += 1
                    continue
                pending[pool.submit(worker, next_index, case)] = next_index
                next_index += 1
            if not pending:
                if stop and next_index < len(selected):
                    for index in range(next_index, len(selected)):
                        results[index] = skip_result(
                            index, selected[index], "not run (max-fail reached)"
                        )
                    break
                continue
            done, _ = wait(pending, return_when=FIRST_COMPLETED)
            for future in done:
                index = pending.pop(future)
                try:
                    result = future.result()
                except Exception as exc:  # noqa: BLE001 - keep the suite alive
                    result = Result(case=selected[index], status="FAIL", failures=[f"harness error: {exc}"])
                results[index] = result
                if result.status in ("FAIL", "XPASS"):
                    failures_seen += 1
                    if args.max_fail and failures_seen >= args.max_fail:
                        stop = True
        if stop:
            for index in range(len(selected)):
                if results[index] is None:
                    results[index] = skip_result(
                        index, selected[index], "not run (max-fail reached)"
                    )

    final_results = [result for result in results if result is not None]
    total_ms = int((time.perf_counter() - start) * 1000)

    if args.json:
        print(
            json.dumps(
                results_to_json(
                    final_results,
                    run_root=run_root,
                    probe=probe,
                    shims=shims,
                    full=full,
                    jobs=args.jobs,
                    total_ms=total_ms,
                ),
                indent=2,
            )
        )
    else:
        print(
            f"cp3c: {cp3c}\nrun:  {run_root}\nmode: {'full' if full else 'fast'}  "
            f"jobs: {args.jobs}  cases: {len(final_results)}"
        )
        for warning in shims.warnings:
            print(f"warning: {warning}")
        print()
        for result in final_results:
            print(status_line(result, full))
            print_result_detail(result)
        print_summary(final_results)
        failed = sum(1 for result in final_results if result.status in ("FAIL", "XPASS"))
        print()
        print(
            f"{len(final_results) - failed} ok, {failed} failing, "
            f"{sum(1 for r in final_results if r.status == 'SKIP')} skipped, "
            f"{sum(1 for r in final_results if r.status == 'XFAIL')} xfail "
            f"({total_ms} ms)"
        )

    cleanup_run(run_root, final_results, args.keep)
    if not args.keep:
        if any(result.status in ("FAIL", "XPASS") for result in final_results):
            print(f"failing case directories kept under {run_root}", file=sys.stderr)
        else:
            shutil.rmtree(run_root, ignore_errors=True)

    failed = any(result.status in ("FAIL", "XPASS") for result in final_results)
    return EXIT_FAILURE if failed else EXIT_SUCCESS


if __name__ == "__main__":
    sys.exit(main())
