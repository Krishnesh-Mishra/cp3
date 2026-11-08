#!/usr/bin/env python3
# =============================================================================
# vendor/tools/check-prerequisites.py - verify (and repair) the host setup.
#
# Checks everything cp3 needs to build and package: the compiler toolchain,
# CMake/Ninja, (optionally) cross toolchains for the arm64 targets, the MSI
# prerequisites and the editor pieces. When something is missing and the host
# allows it (winget on Windows, apt-get on Linux), it says so out loud and
# installs it:
#
#   [missing] clang++ - installing (winget LLVM.LLVM) ... ok
#   [missing] cmake   - installing (apt-get cmake) ... ok
#
# Installs are only performed with consent: interactive mode asks per item
# ([Y/n]), --yes installs everything without asking, --report prints the
# findings and changes nothing. Exit code is 0 when every REQUIRED item is
# present afterwards, 1 otherwise.
#
# Usage:
#   python vendor/tools/check-prerequisites.py            interactive check + offer to repair
#   python vendor/tools/check-prerequisites.py --report   print findings only, change nothing
#   python vendor/tools/check-prerequisites.py --yes      install anything missing without asking
# =============================================================================
from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IS_WINDOWS = os.name == "nt"
IS_LINUX = platform.system() == "Linux"

# --------------------------------------------------------------------------
# output helpers
# --------------------------------------------------------------------------

def ok(name: str, detail: str = "") -> None:
    suffix = f"  {detail}" if detail else ""
    print(f"[ ok ] {name}{suffix}")


def warn(name: str, detail: str = "") -> None:
    suffix = f"  {detail}" if detail else ""
    print(f"[warn] {name}{suffix}")


def missing(name: str, detail: str = "") -> None:
    suffix = f"  {detail}" if detail else ""
    print(f"[missing] {name}{suffix}")


def info(message: str) -> None:
    print(f"         {message}")


def _find(name: str) -> str | None:
    found = shutil.which(name)
    return str(found) if found else None


def _version_of(cmd: list[str], pattern: str) -> str | None:
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = (proc.stdout or "") + (proc.stderr or "")
    match = re.search(pattern, text)
    return match.group(1) if match else None


def _run(cmd: list[str], *, quiet: bool = False, timeout: int = 900) -> int:
    if not quiet:
        printable = " ".join(cmd)
        info(f"$ {printable}")
    try:
        return subprocess.run(cmd, timeout=timeout).returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        info(f"failed to run {cmd[0]}: {exc}")
        return 1


# --------------------------------------------------------------------------
# install helpers
# --------------------------------------------------------------------------

def _winget_id() -> str | None:
    return _find("winget")


def _sudo_prefix() -> list[str]:
    if IS_WINDOWS:
        return []
    try:
        euid = os.geteuid()  # type: ignore[attr-defined]
    except AttributeError:
        euid = 1
    return [] if euid == 0 else ["sudo"]


def _apt_install(packages: list[str], *, update: bool = True) -> int:
    sudo = _sudo_prefix()
    if update:
        rc = _run(sudo + ["apt-get", "update"])
        if rc != 0:
            return rc
    return _run(sudo + ["apt-get", "install", "-y"] + packages)


def _winget_install(package_id: str, *, source: str | None = None) -> int:
    cmd = ["winget", "install", "--id", package_id, "-e", "--silent",
           "--accept-package-agreements", "--accept-source-agreements"]
    if source:
        cmd += ["--source", source]
    return _run(cmd)


class Installer:
    def __init__(self, assume_yes: bool, report_only: bool):
        self.assume_yes = assume_yes
        self.report_only = report_only
        self.changed: list[str] = []

    def prompt(self, what: str) -> bool:
        if self.assume_yes:
            print(f"         installing {what} (--yes) ...")
            return True
        try:
            reply = input(f"         install {what} now? [Y/n] ").strip().lower()
        except EOFError:
            reply = "n"
        return reply in ("", "y", "yes")

    def install(self, what: str, action) -> bool:
        if self.report_only:
            info(f"would install {what} (report-only); run with --yes to configure")
            return False
        if not self.prompt(what):
            info(f"skipped {what}")
            return False
        print(f"         this machine does not have {what}; configuring it now ...")
        rc = action()
        if rc == 0:
            self.changed.append(what)
            ok(what, "installed")
            return True
        info(f"installing {what} failed (exit {rc})")
        return False


# --------------------------------------------------------------------------
# individual checks. Each returns True when the item is usable afterwards.
# --------------------------------------------------------------------------

def check_git(inst: Installer) -> bool:
    if _find("git"):
        ok("git", _version_of(["git", "--version"], r"git version (\S+)") or "")
        return True
    if IS_WINDOWS:
        rc = inst.install("git", lambda: _winget_install("Git.Git"))
    elif IS_LINUX:
        rc = inst.install("git", lambda: _apt_install(["git"]))
    else:
        rc = False
    return rc


def check_cmake(inst: Installer) -> bool:
    found = _find("cmake")
    if found:
        version = _version_of(["cmake", "--version"], r"cmake version (\S+)")
        major = int((version or "0").split(".")[0])
        minor = int((version or "0.0").split(".")[1]) if version and "." in version else 0
        if version and (major > 3 or (major == 3 and minor >= 20)):
            ok("cmake", version)
            return True
        warn("cmake", f"{version or found} is too old (need 3.20+)")
    if IS_WINDOWS:
        rc = inst.install("cmake", lambda: _winget_install("Kitware.CMake"))
    elif IS_LINUX:
        rc = inst.install("cmake", lambda: _apt_install(["cmake"]))
    else:
        rc = False
    return rc


def check_ninja(inst: Installer) -> bool:
    if _find("ninja"):
        ok("ninja", _version_of(["ninja", "--version"], r"(\S+)") or "")
        return True
    if IS_WINDOWS:
        rc = inst.install("ninja", lambda: _winget_install("Ninja-build.Ninja"))
    elif IS_LINUX:
        rc = inst.install("ninja", lambda: _apt_install(["ninja-build"]))
    else:
        rc = False
    return rc


def check_make(inst: Installer) -> bool:
    if IS_WINDOWS:
        # make is not required on Windows (Ninja drives the build)
        return True
    if _find("make"):
        ok("make", _version_of(["make", "--version"], r"GNU Make (\S+)") or "")
        return True
    if IS_LINUX:
        rc = inst.install("make", lambda: _apt_install(["make"]))
    else:
        rc = False
    return rc


def check_cpp_toolchain(inst: Installer) -> bool:
    if IS_WINDOWS:
        # the Windows release builds with the MinGW g++ (MSYS2 ucrt64)
        gpp = _find("g++") or (Path("C:/msys64/ucrt64/bin/g++.exe").is_file() and
                               "C:\\msys64\\ucrt64\\bin\\g++.exe")
        if gpp:
            version = _version_of([str(gpp), "--version"], r"\) (\S+)")
            if gpp == "C:\\msys64\\ucrt64\\bin\\g++.exe":
                warn("g++ (MinGW)", f"{version or gpp} - not on PATH; add C:\\msys64\\ucrt64\\bin to PATH")
                return False
            ok("g++ (MinGW)", version or str(gpp))
            return True
        # not found: offer MSYS2 + the ucrt64 toolchain
        def install_msys2() -> int:
            rc = _winget_install("MSYS2.MSYS2")
            if rc != 0:
                return rc
            pacman = Path(os.environ.get("MSYS2_ROOT", r"C:\msys64")) / "usr" / "bin" / "pacman.exe"
            if not pacman.is_file():
                info(f"pacman not found at {pacman}; open the MSYS2 shell and run:")
                info("  pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc")
                return 1
            return _run([str(pacman), "-S", "--needed", "--noconfirm",
                         "mingw-w64-ucrt-x86_64-gcc",
                         "mingw-w64-ucrt-x86_64-cmake",
                         "mingw-w64-ucrt-x86_64-ninja"])
        rc = inst.install("MinGW g++ (MSYS2)", install_msys2)
        if rc:
            info("MinGW g++ lives in C:\\msys64\\ucrt64\\bin - add it to PATH and re-run this check")
        return rc
    # Linux: g++ builds cp3c itself
    if _find("g++"):
        ok("g++", _version_of(["g++", "--version"], r"\) (\S+)") or "")
        return True
    if IS_LINUX:
        rc = inst.install("g++", lambda: _apt_install(["g++"]))
    else:
        rc = False
    return rc


def check_clang(inst: Installer) -> bool:
    clang = _find("clang++")
    if not clang and IS_WINDOWS:
        # the LLVM installer drops clang++.exe here when it is not on PATH
        for candidate in (Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang++.exe",):
            if candidate.is_file():
                clang = str(candidate)
                break
    if clang:
        version = _version_of([clang, "--version"], r"clang(?:\+\+)? version (\S+)") or \
                  _version_of([clang, "--version"], r"(\d+\.\d+\.\d+)")
        major = 0
        if version:
            try:
                major = int(version.split(".")[0])
            except ValueError:
                major = 0
        if major >= 16:
            if _find("clang++") or IS_LINUX:
                ok("clang++", version or clang)
            else:
                ok("clang++", f"{version or clang} (not on PATH; cp3 looks in the LLVM location too)")
            return True
        warn("clang++", f"{version or clang} is too old (need 16+)")
    if IS_WINDOWS:
        rc = inst.install("clang (LLVM)", lambda: _winget_install("LLVM.LLVM"))
    elif IS_LINUX:
        rc = inst.install("clang", lambda: _apt_install(["clang"]))
    else:
        rc = False
    return rc


def check_wsl(inst: Installer) -> bool:
    if not IS_WINDOWS:
        return True
    if _find("wsl"):
        try:
            rc = subprocess.run(["wsl", "bash", "-lc", "true"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                timeout=60).returncode
        except (OSError, subprocess.TimeoutExpired):
            rc = 1
        if rc == 0:
            ok("WSL", "present")
            return True
        warn("WSL", "present but not working (run 'wsl --install' once in an elevated shell)")
        return False
    # installing WSL requires admin + reboot; never do it unattended
    warn("WSL", "not found - needed for Linux targets (run 'wsl --install' in an elevated shell)")
    return False


def check_cross_toolchain(inst: Installer) -> bool:
    # optional: only needed for the linux-arm64 target
    if not IS_LINUX and not IS_WINDOWS:
        return True
    if IS_LINUX:
        if _find("aarch64-linux-gnu-g++"):
            ok("aarch64-linux-gnu g++", "present (linux-arm64 cross)")
            return True
        missing("aarch64-linux-gnu g++", "optional; needed for the linux-arm64 target")
        installed = inst.install("aarch64-linux-gnu toolchain",
                                 lambda: _apt_install(["gcc-aarch64-linux-gnu",
                                                       "g++-aarch64-linux-gnu"]))
        return installed or True  # optional: never fail the check over it
    # Windows: WSL carries the cross toolchain
    try:
        rc = subprocess.run(["wsl", "bash", "-lc", "command -v aarch64-linux-gnu-g++"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            timeout=60).returncode
    except (OSError, subprocess.TimeoutExpired):
        rc = 1
    if rc == 0:
        ok("aarch64-linux-gnu g++", "present in WSL (linux-arm64 cross)")
    else:
        warn("aarch64-linux-gnu g++", "not in WSL; run inside WSL: apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu")
    return True  # optional


def check_qemu(inst: Installer) -> bool:
    # optional: smoke-runs the linux-arm64 compiler from x64
    if IS_LINUX:
        found = _find("qemu-aarch64-static") or _find("qemu-aarch64")
        if found:
            ok("qemu-aarch64", found)
        else:
            warn("qemu-aarch64", "not found (optional; linux-arm64 --version smoke runs skipped)")
        return True
    if IS_WINDOWS:
        try:
            rc = subprocess.run(["wsl", "bash", "-lc",
                                 "command -v qemu-aarch64-static || command -v qemu-aarch64"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                timeout=60).returncode
        except (OSError, subprocess.TimeoutExpired):
            rc = 1
        if rc == 0:
            ok("qemu-aarch64", "present in WSL (linux-arm64 smoke)")
        else:
            warn("qemu-aarch64", "not in WSL (optional; linux-arm64 smoke runs skipped)")
    return True  # optional


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


def check_llvm_mingw(inst: Installer) -> bool:
    # optional: only needed for the windows-arm64 target; Windows only.
    # presence-only check: never downloads anything, never fails the host check
    if not IS_WINDOWS:
        return True
    found = _llvm_mingw_root()
    if found:
        root, source = found
        ok("llvm-mingw (windows-arm64 toolchain)", f"{root} (via {source})")
        return True
    warn("llvm-mingw (windows-arm64 toolchain)",
         "optional; not found - needed for the windows-arm64 target")
    info("install llvm-mingw-20261104 (ucrt, x86_64) and extract it:")
    info("  https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip")
    info("  sha256: 552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f")
    info(r"  extract to %USERPROFILE%\tools\llvm-mingw")
    info(r"  (so that %USERPROFILE%\tools\llvm-mingw\bin\aarch64-w64-mingw32-clang++.exe exists)")
    info("or set CP3_MINGW_ROOT_ARM64 to the llvm-mingw install root")
    if os.environ.get("CP3_MINGW_ROOT_ARM64"):
        info(f"note: CP3_MINGW_ROOT_ARM64 is set to '{os.environ['CP3_MINGW_ROOT_ARM64']}' "
             r"but bin\aarch64-w64-mingw32-clang++.exe was not found there")
    return True  # optional: never fail the check over it


def check_dotnet(inst: Installer) -> bool:
    # optional: only the MSI build needs the .NET SDK (WiX tool restore)
    if _find("dotnet"):
        ok("dotnet", _version_of(["dotnet", "--version"], r"(\S+)") or "present")
        return True
    warn("dotnet", "not found (optional; needed only for the MSI package)")
    if IS_WINDOWS:
        inst.install(".NET SDK (for the MSI build)",
                     lambda: _winget_install("Microsoft.DotNet.SDK.8"))
    return True  # optional


def check_vscode(inst: Installer) -> bool:
    # optional: only the editor extension packaging needs VS Code
    code = _find("code")
    if code:
        ok("VS Code", code)
        return True
    warn("VS Code", "not found (optional; needed only for the extension)")
    if IS_WINDOWS:
        inst.install("VS Code", lambda: _winget_install("Microsoft.VisualStudioCode"))
    return True  # optional


def check_node(inst: Installer) -> bool:
    # optional: only the extension build needs Node.js
    node = _find("node")
    if node:
        ok("node", _version_of(["node", "--version"], r"v(\S+)") or node)
        return True
    warn("node", "not found (optional; needed only for the extension)")
    return True  # optional


def check_python(inst: Installer) -> bool:
    if sys.version_info >= (3, 8):
        ok("python", sys.version.split()[0])
        return True
    missing("python", f"{sys.version.split()[0]} - need 3.8+")
    return False


CHECKS = [
    ("python", check_python),
    ("git", check_git),
    ("cmake", check_cmake),
    ("ninja", check_ninja),
    ("make", check_make),
    ("C++ toolchain", check_cpp_toolchain),
    ("clang++", check_clang),
    ("WSL", check_wsl),
    ("aarch64 cross toolchain", check_cross_toolchain),
    ("qemu-aarch64", check_qemu),
    ("llvm-mingw (windows-arm64 toolchain)", check_llvm_mingw),
    ("dotnet", check_dotnet),
    ("VS Code", check_vscode),
    ("node", check_node),
]

REQUIRED = {"python", "git", "cmake", "ninja", "make", "C++ toolchain", "clang++"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="verify (and repair) the cp3 host setup")
    parser.add_argument("--report", action="store_true",
                        help="print findings only; install nothing")
    parser.add_argument("--yes", action="store_true",
                        help="install missing pieces without asking")
    args = parser.parse_args(argv)

    inst = Installer(assume_yes=args.yes, report_only=args.report)
    failures = []
    for name, check in CHECKS:
        good = check(inst)
        if not good and name in REQUIRED:
            failures.append(name)

    print()
    if failures:
        print(f"not ready: {', '.join(failures)}")
        if not args.yes and not args.report:
            print("re-run with --yes to configure everything automatically")
        return 1
    print("host ready for cp3.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
