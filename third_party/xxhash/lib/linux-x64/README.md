# xxHash — linux-x64 (PLACEHOLDER)

**No archive here yet.** This directory is a placeholder created on a
non-Linux host; `libxxhash.a` has not been built and must not be
referenced by any `native.hpp` platform row until it exists.

Expected contents once built (same layout as
`third_party/libsodium/lib/linux-x64/`):

```
third_party/xxhash/lib/linux-x64/
  libxxhash.a    # gcc -O3 single-TU include/xxhash.c, GNU ar, x86_64 ELF
  VERSION.txt    # copy of third_party/xxhash/VERSION.txt
```

Build it (native Linux x64 host, or via `third_party/build-linux.sh`
conventions) with the vendored recipe:

```sh
third_party/xxhash/build-linux.sh --arch x64
# equivalent manual form:
#   gcc -O3 -std=c99 -Wall -c third_party/xxhash/include/xxhash.c -o xxhash.o
#   ar rcs libxxhash.a xxhash.o && ranlib libxxhash.a
#   cp libxxhash.a third_party/xxhash/lib/linux-x64/libxxhash.a
#   cp third_party/xxhash/VERSION.txt third_party/xxhash/lib/linux-x64/VERSION.txt
```

Pin: `https://github.com/Cyan4973/xxHash.git v0.8.3
e626a72bc2321cd320e953a0ccf1584cad60f363` (see
`third_party/xxhash/VERSION.txt`).

After building, record the archive's byte count and SHA-256 in
`third_party/MANIFEST.md` (Delivered artifacts) and
`docs/design/linux-packaging.md`, then land the `linux-x64` platform
rows in `src/std/hash/native.hpp` and
`src/std/collections/native.hpp` (wiring lines in
`docs/design/linux-packaging.md`). Do not point those rows at
`lib/windows-x64/` (COFF objects) in the meantime.
