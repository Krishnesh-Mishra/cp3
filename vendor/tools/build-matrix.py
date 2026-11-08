#!/usr/bin/env python3
# =============================================================================
# vendor/tools/build-matrix.py - interactive matrix build + package for cp3.
#
# Asks which targets to build (checkbox UI), builds the compiler for each
# selected (OS, ARCH), then stages and packages it. Outputs:
#
#   build/out/windows-x64/raw/cp3c.exe
#   build/out/windows-x64/stage/cp3/          relocatable install tree
#   build/out/windows-x64/msi/cp3-<ver>-windows-x64.msi
#   build/out/windows-arm64/raw/cp3c.exe
#   build/out/windows-arm64/stage/cp3/
#   build/out/windows-arm64/msi/cp3-<ver>-windows-arm64.msi
#   build/out/linux-x64/raw/cp3
#   build/out/linux-x64/stage/cp3/
#   build/out/linux-x64/tarball/cp3-<ver>-linux-x64.tar.gz
#   build/out/linux-arm64/raw/cp3
#   build/out/linux-arm64/stage/cp3/
#   build/out/linux-arm64/tarball/cp3-<ver>-linux-arm64.tar.gz
#
# Combinations that cannot be built on this host are reported with the exact
# missing prerequisite, never silently skipped or half-staged.
#
# Layout is per-target; `build/out/<target>/build` is the fresh CMake tree
# used to produce it (safe to delete between runs).
#
# Usage:
#   python vendor/tools/build-matrix.py               interactive checklist
#   python vendor/tools/build-matrix.py --list        print feasibility, exit
#   python vendor/tools/build-matrix.py --targets windows-x64,linux-x64
#   python vendor/tools/build-matrix.py --targets windows-x64 --yes
#   python vendor/tools/build-matrix.py --targets all --yes
# =============================================================================
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]          # repository root
OUT = ROOT / "build" / "out"
VERSION = (ROOT / "version.txt").read_text(encoding="utf-8").strip()

IS_WINDOWS = os.name == "nt"
IS_LINUX = platform.system() == "Linux"
_MACHINE = platform.machine().lower()
HOST_ARCH = "x64" if _MACHINE in ("x86_64", "amd64") else \
            "arm64" if _MACHINE in ("aarch64", "arm64") else _MACHINE

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def _find(name: str) -> str | None:
    found = shutil.which(name)
    return str(found) if found else None


def _find_wsl() -> bool:
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return False
    try:
        r = subprocess.run([wsl, "bash", "-lc", "true"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=30, cwd=str(ROOT))
        return r.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def _wsl_path() -> str:
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    # wsl.exe eats single backslashes on the command line, so present a
    # forward-slashed path; wslpath accepts C:/... and returns /mnt/c/...
    proc = subprocess.run([wsl, "wslpath", "-u", str(ROOT).replace("\\", "/")],
                          capture_output=True, text=True, timeout=30)
    return proc.stdout.strip()


def _sh(cmd, *, quiet: bool = False) -> int:
    if not quiet:
        print("  $ " + " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, cwd=str(ROOT)).returncode


def _wsl_sh(command: str, *, quiet: bool = False) -> int:
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not quiet:
        print("  $ wsl bash -lc '" + command + "'")
    return subprocess.run([wsl, "bash", "-lc", command], cwd=str(ROOT)).returncode


def _smoke(exe: Path, label: str) -> bool:
    if not exe.is_file():
        print(f"  smoke {label}: binary missing at {exe}")
        return False
    try:
        proc = subprocess.run([str(exe), "--version"],
                              capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"  smoke {label}: cannot run {exe} ({exc})")
        return False
    out = (proc.stdout or proc.stderr or "").strip()
    if proc.returncode != 0:
        print(f"  smoke {label}: exit {proc.returncode}: {out}")
        return False
    print(f"  smoke {label}: {out}")
    return True


def _prepare_fresh(target: str) -> None:
    out = OUT / target
    for sub in ("raw", "stage", "msi", "tarball", "build"):
        shutil.rmtree(out / sub, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)


def _copy_raw(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def _wsl_command_ok(command: str) -> bool:
    """True when `wsl bash -lc '<command>'` exits 0 (presence probes, e.g.
    'command -v qemu-aarch64-static'). Prints nothing."""
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return False
    try:
        rc = subprocess.run([wsl, "bash", "-lc", command],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            timeout=60, cwd=str(ROOT)).returncode
    except (OSError, subprocess.TimeoutExpired):
        return False
    return rc == 0


def _llvm_mingw_root() -> tuple[str, str] | None:
    """Detect an llvm-mingw install root (the windows-arm64 toolchain).

    Returns (root, source) where root is the dir holding
    bin\\aarch64-w64-mingw32-clang++.exe, or None when not found.
    Search order: $CP3_MINGW_ROOT_ARM64, %USERPROFILE%\\tools\\llvm-mingw,
    C:\\llvm-mingw.
    """
    exe = Path("bin") / "aarch64-w64-mingw32-clang++.exe"
    env = os.environ.get("CP3_MINGW_ROOT_ARM64")
    if env:
        root = Path(env)
        if (root / exe).is_file():
            return str(root), "CP3_MINGW_ROOT_ARM64"
    userprofile = os.environ.get("USERPROFILE")
    if userprofile:
        root = Path(userprofile) / "tools" / "llvm-mingw"
        if (root / exe).is_file():
            return str(root), r"%USERPROFILE%\tools\llvm-mingw"
    root = Path(r"C:\llvm-mingw")
    if (root / exe).is_file():
        return str(root), r"C:\llvm-mingw"
    return None


# --------------------------------------------------------------------------
# targets
# --------------------------------------------------------------------------

def _archive(lib: str, arch: str) -> bool:
    libdir = ROOT / "vendor" / "third_party" / lib / "lib" / arch
    return libdir.is_dir()


LINUX_ARCHIVES_X64 = ("libsodium", "libuv", "uWebSockets", "xxhash",
                      "simdutf", "pcre2", "openssl")

ARM64_ARCHIVE_FILES = {
    "libsodium": ("libsodium.a",),
    "libuv": ("libuv.a",),
    "uWebSockets": ("libuSockets.a",),
    "xxhash": ("libxxhash.a",),
    "simdutf": ("libsimdutf.a",),
    "pcre2": ("libpcre2-8.a",),
    "openssl": ("libssl.a", "libcrypto.a"),
}


def _missing_arm64_archives(arch: str) -> list[str]:
    """Specific archives missing under vendor/third_party/<lib>/lib/<arch>/,
    reported as '<lib>/<archive>'."""
    missing = []
    for lib, names in ARM64_ARCHIVE_FILES.items():
        libdir = ROOT / "vendor" / "third_party" / lib / "lib" / arch
        for name in names:
            if not (libdir / name).is_file():
                missing.append(f"{lib}/{name}")
    return missing


def _windows_x64():
    t = Target("windows-x64", "Windows x64", "windows", IS_WINDOWS, [])
    if not IS_WINDOWS:
        t.missing.append("not a Windows host")
        return t
    for tool in ("cmake", "ninja", "g++"):
        if not _find(tool):
            t.missing.append(f"{tool} not on PATH")
    for lib in ("libsodium", "libuv", "uWebSockets", "xxhash", "simdutf", "pcre2", "openssl"):
        if not (ROOT / "vendor" / "third_party" / lib / "lib" / "windows-x64").is_dir() \
                and not (ROOT / "vendor" / "third_party" / lib / "lib").glob("**/lib*.a"):
            t.missing.append(f"vendor/third_party/{lib}: no windows-x64 archives")
    return t


def _windows_arm64():
    t = Target("windows-arm64", "Windows arm64", "windows", IS_WINDOWS, [])
    if not IS_WINDOWS:
        t.missing.append("not a Windows host")
        return t
    if _llvm_mingw_root() is None:
        t.missing.append("no llvm-mingw toolchain (set CP3_MINGW_ROOT_ARM64, "
                         r"or install to %USERPROFILE%\tools\llvm-mingw or C:\llvm-mingw)")
    archive_missing = _missing_arm64_archives("windows-arm64")
    if archive_missing:
        t.missing.append("no windows-arm64 archives: " + ", ".join(archive_missing))
    return t


def _linux_x64():
    t = Target("linux-x64", "Linux x64", "linux",
               IS_LINUX and HOST_ARCH == "x64" or (IS_WINDOWS and _find_wsl()), [])
    missing = [lib for lib in LINUX_ARCHIVES_X64 if not _archive(lib, "linux-x64")]
    if missing:
        t.missing.append("no linux-x64 archives: " + ", ".join(missing))
    if IS_WINDOWS and not _find_wsl():
        t.missing.append("no WSL environment to build the linux target")
    if not IS_WINDOWS and not IS_LINUX:
        t.missing.append("unsupported host")
    return t


def _linux_arm64():
    t = Target("linux-arm64", "Linux arm64", "linux", False, [])
    if IS_LINUX and HOST_ARCH == "arm64":
        t.host_ok = True
    elif IS_LINUX:
        # native x86_64 (or other) Linux with the aarch64 cross g++
        t.host_ok = bool(_find("aarch64-linux-gnu-g++"))
        if not t.host_ok:
            t.missing.append("aarch64-linux-gnu-g++ not on PATH (linux-arm64 cross; "
                             "apt-get install -y g++-aarch64-linux-gnu)")
    elif IS_WINDOWS:
        if not _find_wsl():
            t.missing.append("no WSL environment to build the linux-arm64 target")
        elif not _wsl_command_ok("command -v aarch64-linux-gnu-g++"):
            t.missing.append("aarch64-linux-gnu-g++ not in WSL (run inside WSL: "
                             "apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu)")
        else:
            t.host_ok = True
    else:
        t.missing.append("unsupported host")
    missing_archives = _missing_arm64_archives("linux-arm64")
    if missing_archives:
        t.missing.append("no linux-arm64 archives: " + ", ".join(missing_archives))
    return t


class Target:
    def __init__(self, key, title, kind, host_ok, missing):
        self.key = key
        self.title = title
        self.kind = kind
        self.host_ok = host_ok
        self.missing = list(missing)
        self.selected = False

    def available(self):
        return self.host_ok and not self.missing

    def status(self):
        if self.available():
            return "ready"
        reasons = list(self.missing)
        if not self.host_ok:
            reasons.insert(0, "not supported on this host")
        return "unavailable: " + "; ".join(reasons)

    def build_dir(self):
        return OUT / self.key / "build"

    def raw_name(self):
        return "cp3c.exe" if self.kind == "windows" else "cp3"

    def raw_path(self):
        return OUT / self.key / "raw" / self.raw_name()

    def stage_tree(self):
        return OUT / self.key / "stage" / "cp3"


TARGETS = [
    _windows_x64(),
    _windows_arm64(),
    _linux_x64(),
    _linux_arm64(),
]


def _selected_targets(keys):
    picked = []
    for key in keys:
        match = next((t for t in TARGETS if t.key == key), None)
        if match:
            picked.append(match)
        else:
            print(f"unknown target '{key}'", file=sys.stderr)
            return None
    return picked


# --------------------------------------------------------------------------
# interactive checkbox UI
# --------------------------------------------------------------------------

def _render(targets, selected, cursor):
    lines = [
        f"cp3 matrix build v{VERSION}",
        "space = toggle   a = all   n = none   Enter = build   q = quit",
        "",
    ]
    for i, t in enumerate(targets):
        mark = "[x]" if i in selected else "[ ]"
        caret = ">" if i == cursor else " "
        lines.append(f"{caret} {mark} {t.title:<16} {t.status()}")
    return "\n".join(lines)


def _read_key():
    if os.name == "nt":
        import msvcrt

        def read():
            ch = msvcrt.getwch()
            if ch == "\x03":
                raise KeyboardInterrupt
            if ch in ("\r", "\n"):
                return "enter"
            if ch == " ":
                return "space"
            if ch in ("\x00", "\xe0"):
                # Windows extended key: getwch() returns chr(0) or chr(224) first,
                # then one scan-code letter ('H'=72 up, 'P'=80 down, 'K'=75 left,
                # 'M'=77 right, 'G'=71 home, 'O'=79 end)
                scan = msvcrt.getwch()
                return {"H": "up", "P": "down", "K": "left", "M": "right",
                        "G": "home", "O": "end"}.get(scan, "unknown")
            if ch == "\x1b":
                second = msvcrt.getwch()
                if second == "[":
                    third = msvcrt.getwch()
                    return {"A": "up", "B": "down", "H": "home"}.get(third, "unknown")
                return "unknown"
            return ch
        return read

    import termios
    import tty

    def read():
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
            if ch == "\x03":
                raise KeyboardInterrupt
            if ch in ("\r", "\n"):
                return "enter"
            if ch == " ":
                return "space"
            if ch == "\x1b":
                seq = sys.stdin.read(2)
                return {"[A": "up", "[B": "down", "[H": "home"}.get(seq, "unknown")
            return ch
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    return read


def interactive_select(targets):
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        return [t.key for t in targets if t.available()]
    selected = {i for i, t in enumerate(targets) if t.available()}
    cursor = 0
    read = _read_key()
    try:
        while True:
            sys.stdout.write("\x1b[2J\x1b[H" +
                             _render(targets, selected, cursor) + "\n")
            sys.stdout.flush()
            key = read()
            if key == "up":
                cursor = (cursor - 1) % len(targets)
            elif key == "down":
                cursor = (cursor + 1) % len(targets)
            elif key == "home":
                cursor = 0
            elif key == "space":
                if targets[cursor].available():
                    if cursor in selected:
                        selected.remove(cursor)
                    else:
                        selected.add(cursor)
            elif key == "a":
                selected = {i for i, t in enumerate(targets) if t.available()}
            elif key == "n":
                selected = set()
            elif key == "q":
                return None
            elif key == "enter":
                return [targets[i].key for i in sorted(selected)]
    except KeyboardInterrupt:
        return None


def parse_targets_arg(value):
    if value is None:
        return None
    value = value.lower().strip()
    if value == "all":
        return [t.key for t in TARGETS if t.available()]
    return [v.strip() for v in value.split(",") if v.strip()]


# --------------------------------------------------------------------------
# build + package
# --------------------------------------------------------------------------

def cmake_configure(bdir: Path, generator: str) -> int:
    return _sh(["cmake", "-S", str(ROOT), "-B", str(bdir), "-G", generator,
                "-DCMAKE_BUILD_TYPE=Release"])


def cmake_build(bdir: Path) -> int:
    return _sh(["cmake", "--build", str(bdir), "--parallel",
                os.environ.get("CP3_BUILD_PARALLEL", "20")])


def build_windows_x64(t: Target) -> int:
    bdir = t.build_dir()
    if cmake_configure(bdir, "Ninja") != 0:
        return 1
    if cmake_build(bdir) != 0:
        return 1
    src = bdir / "bin" / "cp3c.exe"
    if not src.is_file():
        print("  build failed: no compiler produced")
        return 1
    _copy_raw(src, t.raw_path())
    print(f"  raw: {t.raw_path()}")
    return 0


def package_windows_x64(t: Target) -> int:
    if not _smoke(t.build_dir() / "bin" / "cp3c.exe", "windows-x64"):
        return 1
    rc = _sh(["powershell", "-ExecutionPolicy", "Bypass", "-File",
              str(ROOT / "dist" / "packaging" / "windows" / "package.ps1"),
              "-BuildDir", str(t.build_dir()),
              "-OutDir", str(t.stage_tree().parent),
              "-NoVsix"])
    if rc != 0:
        return 1
    msi_dir = OUT / t.key / "msi"
    msi_dir.mkdir(parents=True, exist_ok=True)
    rc = _sh(["powershell", "-ExecutionPolicy", "Bypass", "-File",
              str(ROOT / "dist" / "packaging" / "windows" / "build-msi.ps1"),
              "-StageDir", str(t.stage_tree()),
              "-OutDir", str(msi_dir),
              "-Arch", "x64"])
    if rc != 0:
        print(f"  warning: MSI build failed; staged tree at {t.stage_tree()} remains")
        return 1
    for artifact in msi_dir.glob("*.msi"):
        print(f"  msi: {artifact}")
    return 0


def build_linux_x64_wsl(t: Target) -> int:
    rp = _wsl_path()
    rp_build = f"{rp}/build/out/{t.key}/build"
    cmd = (f"cmake -S {rp} -B {rp_build} -G Ninja -DCMAKE_BUILD_TYPE=Release && "
           f"cmake --build {rp_build} --parallel {os.environ.get('CP3_BUILD_PARALLEL', '20')}")
    if _wsl_sh(cmd) != 0:
        return 1
    bdir = t.build_dir()
    candidates = [bdir / "bin" / "cp3c", bdir / "bin" / "cp3"]
    chosen = next((c for c in candidates if c.is_file()), None)
    if chosen is None:
        print("  build failed: no compiler produced")
        return 1
    _copy_raw(chosen, t.raw_path())
    t.raw_path().chmod(0o755)
    print(f"  raw: {t.raw_path()}")
    return 0


def build_linux_native(t: Target) -> int:
    bdir = t.build_dir()
    if cmake_configure(bdir, "Ninja") != 0:
        return 1
    if cmake_build(bdir) != 0:
        return 1
    src = bdir / "bin" / "cp3c"
    exe = bdir / "bin" / "cp3"
    src = src if src.is_file() else exe
    if not src.is_file():
        print("  build failed: no compiler produced")
        return 1
    _copy_raw(src, t.raw_path())
    t.raw_path().chmod(0o755)
    print(f"  raw: {t.raw_path()}")
    return 0


def package_linux_x64(t: Target) -> int:
    exe = OUT / t.key / "raw" / "cp3"
    if IS_WINDOWS:
        _wsl_sh(f"{_wsl_path()}/build/out/{t.key}/raw/cp3 --version", quiet=True)
    else:
        _smoke(exe, "linux-x64")
    # --out-dir must be visible inside WSL as the repo mount; use a repo-relative
    # path. package.sh stages <out-dir>/cp3, install.sh, tarball and is idempotent.
    stage_out = OUT / t.key / "stage"
    stage_out.mkdir(parents=True, exist_ok=True)
    if IS_WINDOWS:
        cmd = (f"bash dist/packaging/linux/package.sh "
               f"--build-dir build/out/{t.key}/build "
               f"--out-dir build/out/{t.key}/stage "
               f"--third-party-dir vendor/third_party "
               f"--no-vsix --clean")
        if _wsl_sh(cmd) != 0:
            return 1
    else:
        if _sh(["bash", "dist/packaging/linux/package.sh",
                "--build-dir", str(t.build_dir()),
                "--out-dir", str(stage_out),
                "--third-party-dir", str(ROOT / "vendor" / "third_party"),
                "--no-vsix", "--clean"]) != 0:
            return 1
    # layout contract: tarball lands in tarball/, stage stays in stage/
    tar_dir = OUT / t.key / "tarball"
    tar_dir.mkdir(parents=True, exist_ok=True)
    for artifact in stage_out.glob("cp3-*.tar.gz"):
        shutil.move(str(artifact), str(tar_dir / artifact.name))
        print(f"  tarball: {tar_dir / artifact.name}")
    return 0


def build_linux_arm64(t: Target) -> int:
    bdir = t.build_dir()
    parallel = os.environ.get("CP3_BUILD_PARALLEL", "20")
    if IS_WINDOWS:
        rp = _wsl_path()
        rp_build = f"{rp}/build/out/{t.key}/build"
        cmd = (f"cmake -S '{rp}' -B '{rp_build}' -G Ninja -DCMAKE_BUILD_TYPE=Release "
               f"-DCMAKE_TOOLCHAIN_FILE='{rp}/vendor/tools/cmake/linux-aarch64-toolchain.cmake' && "
               f"cmake --build '{rp_build}' --parallel {parallel}")
        rc = _wsl_sh(cmd)
        if rc != 0:
            return rc
    else:
        cmd = ["cmake", "-S", str(ROOT), "-B", str(bdir), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=Release"]
        if HOST_ARCH != "arm64":
            toolchain = ROOT / "vendor" / "tools" / "cmake" / "linux-aarch64-toolchain.cmake"
            cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        rc = _sh(cmd)
        if rc != 0:
            return rc
        rc = _sh(["cmake", "--build", str(bdir), "--parallel", parallel])
        if rc != 0:
            return rc
    candidates = [bdir / "bin" / "cp3c", bdir / "bin" / "cp3"]
    chosen = next((c for c in candidates if c.is_file()), None)
    if chosen is None:
        print("  build failed: no compiler produced")
        return 1
    _copy_raw(chosen, t.raw_path())
    t.raw_path().chmod(0o755)
    print(f"  raw: {t.raw_path()}")
    return 0


def package_linux_arm64(t: Target) -> int:
    exe = OUT / t.key / "raw" / "cp3"
    stage_out = OUT / t.key / "stage"
    stage_out.mkdir(parents=True, exist_ok=True)
    runner = None
    if IS_WINDOWS:
        if _wsl_command_ok("command -v qemu-aarch64-static"):
            runner = "qemu-aarch64-static -L /usr/aarch64-linux-gnu"
        cmd = (f"bash dist/packaging/linux/package.sh "
               f"--build-dir build/out/{t.key}/build "
               f"--out-dir build/out/{t.key}/stage "
               f"--arch arm64 --no-vsix --clean")
        if runner:
            cmd = f"CP3_PACKAGE_RUNNER='{runner}' {cmd}"
        else:
            # package.sh --no-run: staged compiler is not executed,
            # manifest records compiler_version as unverified
            cmd += " --no-run --untested"
        if _wsl_sh(cmd) != 0:
            return 1
    else:
        cmd = ["bash", str(ROOT / "dist" / "packaging" / "linux" / "package.sh"),
               "--build-dir", str(t.build_dir()),
               "--out-dir", str(stage_out),
               "--arch", "arm64", "--no-vsix", "--clean"]
        if HOST_ARCH == "arm64":
            rc = _sh(cmd)                     # native arm64: runs natively
        else:
            qemu = _find("qemu-aarch64-static") or _find("qemu-aarch64")
            if qemu:
                runner = f"{qemu} -L /usr/aarch64-linux-gnu"
                env = dict(os.environ)
                env["CP3_PACKAGE_RUNNER"] = runner
                print(f"  $ CP3_PACKAGE_RUNNER='{runner}' " + " ".join(cmd))
                rc = subprocess.run(cmd, cwd=str(ROOT), env=env).returncode
            else:
                rc = _sh(cmd + ["--no-run", "--untested"])
        if rc != 0:
            return rc
    # layout contract: tarball lands in tarball/, stage stays in stage/
    tar_dir = OUT / t.key / "tarball"
    tar_dir.mkdir(parents=True, exist_ok=True)
    moved = False
    for artifact in stage_out.glob("cp3-*.tar.gz"):
        shutil.move(str(artifact), str(tar_dir / artifact.name))
        print(f"  tarball: {tar_dir / artifact.name}")
        moved = True
    if not moved:
        print(f"  note: no cp3-*.tar.gz found in {stage_out}")
    if IS_WINDOWS:
        if runner:
            _wsl_sh(f"{_wsl_path()}/build/out/{t.key}/raw/cp3 --version")
        else:
            print("  note: the linux-arm64 compiler cannot be executed on this host "
                  "(no qemu-aarch64-static in WSL); it was recorded as unverified")
    elif HOST_ARCH == "arm64":
        _smoke(exe, "linux-arm64")
    elif runner:
        _sh(runner.split() + [str(exe), "--version"])
    else:
        print("  note: the linux-arm64 binary cannot be executed on this host "
              "(no qemu-aarch64-static on PATH); it was recorded as unverified")
    return 0


def build_windows_arm64(t: Target) -> int:
    bdir = t.build_dir()
    toolchain = ROOT / "vendor" / "tools" / "cmake" / "windows-aarch64-llvm-mingw.cmake"
    cmd = ["cmake", "-S", str(ROOT), "-B", str(bdir), "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release",
           f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"]
    found = _llvm_mingw_root()
    if found:
        root, source = found
        print(f"  llvm-mingw: {root} (via {source})")
        cmd.append(f"-DCP3_MINGW_ROOT_ARM64={Path(root).as_posix()}")
    else:
        # no -DCP3_MINGW_ROOT_ARM64: let the toolchain file apply its default
        print("  llvm-mingw: not detected; toolchain file will apply its default root")
    rc = _sh(cmd)
    if rc != 0:
        return rc
    rc = cmake_build(bdir)
    if rc != 0:
        return rc
    src = bdir / "bin" / "cp3c.exe"
    if not src.is_file():
        print("  build failed: no compiler produced")
        return 1
    _copy_raw(src, t.raw_path())
    print(f"  raw: {t.raw_path()}")
    return 0


def package_windows_arm64(t: Target) -> int:
    # no smoke run of the staged exe: this host is x64 and cannot execute arm64 code.
    # NoRun records compiler_version as unverified; NoRuntimeDlls is safe because
    # the arm64 toolchain links -static (no MinGW runtime DLLs to stage).
    rc = _sh(["powershell", "-ExecutionPolicy", "Bypass", "-File",
              str(ROOT / "dist" / "packaging" / "windows" / "package.ps1"),
              "-BuildDir", str(t.build_dir()),
              "-OutDir", str(t.stage_tree().parent),
              "-Arch", "arm64",
              "-NoVsix", "-NoRun", "-NoRuntimeDlls", "-Untested"])
    if rc != 0:
        return 1
    msi_dir = OUT / t.key / "msi"
    msi_dir.mkdir(parents=True, exist_ok=True)
    rc = _sh(["powershell", "-ExecutionPolicy", "Bypass", "-File",
              str(ROOT / "dist" / "packaging" / "windows" / "build-msi.ps1"),
              "-StageDir", str(t.stage_tree()),
              "-OutDir", str(msi_dir),
              "-Arch", "arm64",
              "-Untested"])
    if rc != 0:
        print(f"  warning: MSI build failed; staged tree at {t.stage_tree()} remains")
        return 1
    for artifact in msi_dir.glob("*.msi"):
        print(f"  msi: {artifact}")
    print("  note: staged windows-arm64 compiler is untested on this host; "
          "the manifest records compiler_version as unverified")
    return 0


BUILDERS = {
    ("windows-x64", "build"): build_windows_x64,
    ("windows-x64", "package"): package_windows_x64,
    ("windows-arm64", "build"): build_windows_arm64,
    ("windows-arm64", "package"): package_windows_arm64,
    ("linux-x64", "build"): build_linux_x64_wsl if IS_WINDOWS else build_linux_native,
    ("linux-x64", "package"): package_linux_x64,
    ("linux-arm64", "build"): build_linux_arm64,
    ("linux-arm64", "package"): package_linux_arm64,
}


def do_target(t: Target) -> int:
    print(f"[{t.key}] {t.title}")
    print(f"  status : {t.status()}")
    if not t.available():
        print("  skipped (unavailable on this host)")
        return 0
    _prepare_fresh(t.key)
    rc = BUILDERS[(t.key, "build")](t)
    if rc != 0:
        print("  build  : FAILED")
        return rc
    rc = BUILDERS[(t.key, "package")](t)
    if rc != 0:
        print("  package: FAILED (build tree left in place)")
        return rc
    print(f"  done   : {OUT / t.key}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="cp3 matrix build + package")
    parser.add_argument("--list", action="store_true",
                        help="print target feasibility and exit")
    parser.add_argument("--targets", default=None,
                        help="comma-separated target keys, or 'all' (default when not a tty: every available target)")
    parser.add_argument("--yes", action="store_true",
                        help="do not ask for confirmation before building")
    args = parser.parse_args(argv)

    if args.list:
        for t in TARGETS:
            print(f"{t.key:<14} {t.title:<16} {t.status()}")
        return 0

    if args.targets is not None:
        keys = parse_targets_arg(args.targets)
        if not keys:
            print("no targets selected", file=sys.stderr)
            return 2
        chosen = _selected_targets(keys)
        if chosen is None:
            return 2
    else:
        picked = interactive_select(TARGETS)
        if picked is None:
            print("aborted.")
            return 1
        chosen = _selected_targets(picked)
        if chosen is None:
            return 2

    skipped = [t for t in chosen if not t.available()]
    chosen = [t for t in chosen if t.available()]
    for t in skipped:
        print(f"  skipping {t.key}: {t.status()}")
    if not chosen:
        print("no buildable targets selected.")
        return 1

    print(f"targets: {', '.join(t.key for t in chosen)}")
    if not args.yes:
        reply = input("continue? [y/N] ").strip().lower()
        if reply not in ("y", "yes"):
            print("aborted.")
            return 1

    failures = 0
    for t in chosen:
        if do_target(t) != 0:
            failures += 1
    if failures:
        print(f"{failures} target(s) failed; see output above")
        return 1
    print("matrix build complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
