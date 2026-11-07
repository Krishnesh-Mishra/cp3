# xxHash - linux-x64

`libxxhash.a` here is a real Linux x64 archive (ELF objects), not a
placeholder.

- bytes: 67096
- SHA-256: 3DA25C3EDB7660788D0DE4E6905CD04EFBABFC1F0D6FD57AE6FF19535CB15B83
- built: 2026-11-07, WSL2 Ubuntu, GCC 15.2.0, GNU ar/ranlib
- source: pinned v0.8.3 (`third_party/xxhash/VERSION.txt`)
- recipe: `third_party/xxhash/build-linux.sh --arch x64` (compiles
  `include/xxhash.c` with `-O3`, then `ar rcs` + `ranlib`)

Wired into the `linux-x64` platform rows in `src/std/hash/native.hpp` and
`src/std/collections/native.hpp`. Do not point any row at
`lib/windows-x64/` (COFF objects); `linux-arm64` has no archive yet, so it
has no platform row and fails loudly at native-package resolution.
