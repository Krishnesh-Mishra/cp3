# cp3 Linux packaging

Stages the Linux distribution and writes a per-user tarball:

```text
build/out/linux-x64/
  cp3/                                  relocatable install tree
  install.sh                            per-user installer (no root)
  cp3-0.2.0-linux-x64.tar.gz
  SHA256SUMS.txt
```

## Pipeline

```text
vendor/tools/build.sh                  sync version.txt, cmake configure + build
  |                             -> build/bin/cp3c
  v
dist/packaging/linux/package.sh      stage build/out/linux-<arch>/cp3, manifest.json,
  |                             smoke test, install.sh, tar.gz
  v
build/out/linux-<arch>/         artifact + SHA256SUMS.txt
```

From the repository root:

```sh
# build + package
./vendor/tools/build.sh

# package an existing build only
dist/packaging/linux/package.sh --build-dir build --out-dir build/out/linux-x64

# skip the hello-world smoke test (no Clang on the build machine)
dist/packaging/linux/package.sh --skip-smoke

# package using the Linux-built vendor archives (build them first; the
# default third-party tree is vendor/third_party, no flag needed)
bash vendor/third_party/build-linux.sh --arch x64
dist/packaging/linux/package.sh
```

## Stage layout

Same contract as the Windows package:

| Path | Contents |
| --- | --- |
| `bin/cp3c` | the compiler (also the LSP server via `cp3c lsp`) |
| `bin/cp3` | the `cp3` command: `init`, `doctor`, and everything `cp3c` offers |
| `include/` | `runtime/`, `common/`, `std/<family>/` header trees |
| `src/` | runtime translation units compiled into every cp3 program |
| `vendor/third_party/` | vendored static libraries |
| `share/vscode/` | bundled VS Code extension, when present |
| `manifest.json` | name, version, compiler version, `built_at`, per-file size + SHA-256 |
| `README.txt` | end-user quick start |

The `bin/cp3` shim sets `CP3_ROOT` to the install root before forwarding to
`bin/cp3c`, so the tree works from any directory and through a `PATH` symlink.

## Install

```sh
tar -xzf cp3-0.2.0-linux-x64.tar.gz
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

The checked-in `vendor/third_party/*/lib/*.a` files are Windows MinGW (COFF) vendor
builds. Linux archives are kept within each vendor package under
`vendor/third_party/<vendor>/lib/linux-x64/` or `linux-arm64/`. Build the matching set on Linux with
`bash vendor/third_party/build-linux.sh --arch x64` or `--arch arm64`; arm64 cross builds need an
`aarch64-linux-gnu` toolchain (`CC`, `CXX`, `AR`, and `RANLIB` may be set to
custom tool paths). `package.sh` stages only the selected architecture and
warns if any required archive is missing. Without those archives,
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
