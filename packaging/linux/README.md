# cp3 Linux packaging

Stages the Linux distribution and writes a per-user tarball:

```text
out/linux-x64/
  cp3/                                  relocatable install tree
  install.sh                            per-user installer (no root)
  cp3-0.1.0-linux-x64.tar.gz
  SHA256SUMS.txt
```

## Pipeline

```text
tools/build.sh                  sync version.txt, cmake configure + build
  |                             -> build/bin/cp3c
  v
packaging/linux/package.sh      stage out/linux-<arch>/cp3, manifest.json,
  |                             smoke test, install.sh, tar.gz
  v
out/linux-<arch>/               artifact + SHA256SUMS.txt
```

From the repository root:

```sh
# build + package
./tools/build.sh

# package an existing build only
packaging/linux/package.sh --build-dir build --out-dir out/linux-x64

# skip the hello-world smoke test (no Clang on the build machine)
packaging/linux/package.sh --skip-smoke

# use Linux-built vendor archives
packaging/linux/package.sh --third-party-dir /path/to/linux/third_party
```

## Stage layout

Same contract as the Windows package:

| Path | Contents |
| --- | --- |
| `bin/cp3c` | the compiler (also the LSP server via `cp3c lsp`) |
| `bin/cp3` | the `cp3` command: `init`, `doctor`, and everything `cp3c` offers |
| `include/` | `runtime/`, `common/`, `std/<family>/` header trees |
| `src/` | runtime translation units compiled into every cp3 program |
| `third_party/` | vendored static libraries |
| `share/vscode/` | bundled VS Code extension, when present |
| `manifest.json` | name, version, compiler version, `built_at`, per-file size + SHA-256 |
| `README.txt` | end-user quick start |

The `bin/cp3` shim sets `CP3_ROOT` to the install root before forwarding to
`bin/cp3c`, so the tree works from any directory and through a `PATH` symlink.

## Install

```sh
tar -xzf cp3-0.1.0-linux-x64.tar.gz
./install.sh
```

`install.sh` copies the tree to `$HOME/.local/lib/cp3` and writes
`$HOME/.local/bin/cp3`. Override the prefix with `--prefix`, replace an
existing install with `--force`.

## Requirements

Build machine: CMake 3.20+, Ninja or Make, a C++20 compiler, Clang 16+ for the
staged smoke test, `sha256sum`, `tar`.

End-user machine: Clang 16+ (`clang++`) to compile cp3 programs. The compiler,
headers, runtime sources and vendor libraries are shipped in the tree.

## Third-party archives

The checked-in `third_party/*/lib/*.a` files are the Windows MinGW (COFF)
vendor builds. They are not linkable on Linux. `package.sh` detects them and
prints a warning. Rebuild the vendor tree on Linux and pass
`--third-party-dir <tree>` for a fully self-contained package; otherwise the
compiler and the stdlib families that need no vendor archive work, while
`std/network` and `std/crypto` cannot link.

## Relocatability

`cp3c` resolves its root in this order
(`src/cli/src/toolchain/runtime/util.cpp`):

1. `CP3_ROOT` (the `cp3` shim exports `<install>`);
2. the running executable's ancestors (`<install>/bin/cp3c` -> `<install>`);
3. its own compile-time path (an in-checkout compiler).

Header roots are mapped from `src/<x>/include` to `include/<x>` when the
checkout directory does not exist, so one `include/` tree serves every case
while runtime sources stay under `src/`.
