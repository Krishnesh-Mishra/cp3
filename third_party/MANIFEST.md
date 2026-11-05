# third_party MANIFEST

Pinned, static, reproducible vendor builds used by cp3c for the std/crypto,
std/network and parallel.* families, plus the core regex backend (PCRE2).

Built 2026-10-18 (libsodium, libuv, uWebSockets, oneTBB) and 2026-10-19
(PCRE2) on Windows x64 with:
MinGW-W64 x86_64 ucrt posix-seh GCC 16.1.0 (WinLibs), cmake 4.3.2, ninja,
GNU ar (MinGW binutils), Git for Windows bash, PowerShell 5.1.
-DCMAKE_POLICY_VERSION_MINIMUM=3.5 was NOT needed by any project here
(libuv v1.53.0 and oneTBB v2023.1.0 both configure cleanly with cmake 4.3.2
without it).

Every command below was actually run; the verbatim step scripts live in
build/scratch/vend/scripts/ and third_party/build.ps1 re-runs the whole flow.

## Pins

| library | repo URL | tag | commit (full SHA) |
| --- | --- | --- | --- |
| libsodium 1.0.22 | https://github.com/jedisct1/libsodium.git | 1.0.22-RELEASE | 77e1ce5d6dee871c49ef211222ba18ef0c486bda |
| libuv | https://github.com/libuv/libuv.git | v1.53.0 | 840404ce8ba7cc0204be52389a6cfff9f2c90fb6 |
| uWebSockets (incl. uSockets) | https://github.com/uNetworking/uWebSockets.git | v20.80.0 | 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5 |
| oneTBB | https://github.com/uxlfoundation/oneTBB.git | v2023.1.0 | 3046c8b0c29df995980003ea24f4d78c80ec0c8d |
| PCRE2 | https://github.com/PCRE2Project/pcre2.git | pcre2-10.49 | 6f9d7c1373262c541324a16a358785b33ef116cf |

Each library directory carries VERSION.txt with
"<repo URL> <tag> <full SHA> <date built>". All five checkout trees are
detached at the pinned commit; `git clone --depth 1 --branch <tag> <url>` was
verified to reproduce the four original SHAs exactly (log:
build/scratch/vend/verify/clone_check.txt), and the PCRE2 pin was acquired
with the same command form before being checked out at its SHA.

## Delivered artifacts (bytes, SHA-256)

The Windows archives remain at their original paths for compatibility with
the existing oneTBB/PCRE2 rows. Windows x64 copies for `std/crypto` and
`std/network` are also stored under `lib/windows-x64/`; Linux archives are
stored by target architecture under each vendor's `lib/linux-*/` directory.

| file | bytes | SHA-256 |
| --- | --- | --- |
| libsodium/lib/libsodium.a | 612878 | 53D7D5022EFF7EBED9A64FCF4543EFDCD0D7D09736F4370D9BF4E71D4C79D714 |
| libsodium/lib/windows-x64/libsodium.a | 612878 | 53D7D5022EFF7EBED9A64FCF4543EFDCD0D7D09736F4370D9BF4E71D4C79D714 |
| libsodium/lib/linux-x64/libsodium.a | 977328 | 1156C820E07DBFF6A7B4F89C2A05D33F0C878BCB3E7C66BB8A08E18EB0A5036A |
| libsodium/lib/linux-arm64/libsodium.a | 816242 | 9A95F096AD78CF8536071A52846090FEB55EF7CC474B3FAD6C7B01ADFEBF9F72 |
| libuv/lib/libuv.a | 422742 | 66B0B14A07C1943A989B0A33FCB749A6B7275654797AF5A916BCA0D695852544 |
| libuv/lib/windows-x64/libuv.a | 422742 | 66B0B14A07C1943A989B0A33FCB749A6B7275654797AF5A916BCA0D695852544 |
| libuv/lib/linux-x64/libuv.a | 410466 | C1E0015598BFDC3D344AA52DD470405D8BFDE8DED8AAAF6B2C423F02C44D2EB6 |
| libuv/lib/linux-arm64/libuv.a | 399602 | 82AC581C15C9C406662562F8686D1FE0C21D990FD0550EF8C065C20B2EE51C53 |
| oneTBB/lib/libtbb.a (upstream name libtbb12.a) | 1107372 | 01F8BF7B97AE88C19BCC87B436ABB29C2E339BF6D6D3583BFC315D984A11A15A |
| oneTBB/lib/libtbbmalloc.a | 245472 | 53AC7AAE285488384808E339854F795454046BD345D8B46AAA15AAE0016D08A0 |
| uWebSockets/lib/libuSockets.a | 47432 | 835B8688A8F301533CA7508E309DF2E832D335FFC0FE029BCD0EC3BD87CE5E50 |
| uWebSockets/lib/windows-x64/libuSockets.a | 47432 | 835B8688A8F301533CA7508E309DF2E832D335FFC0FE029BCD0EC3BD87CE5E50 |
| uWebSockets/lib/linux-x64/libuSockets.a | 60848 | 2730C09A296D371FEC538EA81353A2DA52E0FD429FAB26750E391EF37ECD9474 |
| uWebSockets/lib/linux-arm64/libuSockets.a | 63024 | 51ED1EB61035C09F3D525A30019928169DAEF5B32B830EFFF625617DEE9A93BA |
| uWebSockets/lib/libuWebSockets.a | 1438 | EB0D1543E48D0F3077BE44104532502AA5C65E8E632564F24973B9384F9ECBF5 |
| pcre2/lib/libpcre2-8.a | 574408 | 79EF3EBB2F088D5FEC2B96035F2EFB173A18109C47523EF7976113ED7AC2E7E4 |

Rebuilds are not guaranteed byte-identical (ar archives record object file
timestamps); the SHA-256 values above identify exactly the delivered bytes.

## Licenses copied into third_party/

| file | license |
| --- | --- |
| libsodium/LICENSE | ISC |
| libuv/LICENSE | MIT |
| oneTBB/LICENSE.txt | Apache-2.0; oneTBB/third-party-programs.txt has bundled third-party notices |
| uWebSockets/LICENSE | Apache-2.0 |
| uWebSockets/LICENSE-uSockets | Apache-2.0 (uSockets) |
| pcre2/LICENCE.md | BSD-3-Clause WITH PCRE2-exception |
| clang-c/LICENSE | Apache-2.0 WITH LLVM-exception |

## clang-c (headers only, no build)

`clang-c` is the one entry here that is **not** a compiled vendor build: it is
the public libclang C API header set (`clang/include/clang-c/*.h`,
CINDEX_VERSION 0.64) copied verbatim from `llvmorg-23.1.0`
(commit `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`), with the upstream LICENSE
and a VERSION.txt pin.

It exists so that `cp3c native-install` can be **compiled** without an LLVM
installation: `src/cli/src/native/*` needs the libclang C declarations at
compile time, and `src/cli/CMakeLists.txt` adds
`third_party/clang-c/include` to the include path. The libclang *library* is
never vendored and never linked: `src/cli/src/native/libclang.cpp` loads it
dynamically at runtime from the user's own LLVM installation
(`CP3_LIBCLANG` overrides the search), and reports a clear install hint when
it is missing.

Point `-DCP3_LIBCLANG_INCLUDE_DIR=<dir>` at a system LLVM include directory
(for example `C:/Program Files/LLVM/include`) to build against that copy
instead of the vendored headers.

## What got copied

Only what each recipe needs: include/ trees from the upstream install (or from
the upstream source for header-only uWebSockets), lib/*.a, the upstream license
files, and VERSION.txt. Nothing else. One exception worth naming: upstream's
oneTBB install payload includes a C++ module interface file,
include/oneapi/tbb.cppm, so it is part of the delivered include tree. PCRE2
follows the same rule: pcre2/include (pcre2.h, pcre2posix.h), the single
lib/libpcre2-8.a, pcre2/LICENCE.md and pcre2/VERSION.txt. pcre2posix.h ships
although libpcre2-posix.a does not: the regex runtime uses the native 8-bit
API only.

## Build commands (exact)

### libsodium 1.0.22 (autotools, static only)

libsodium 1.0.22 has no CMake build; its own autotools build was used.
Run from Git Bash. Script: build/scratch/vend/scripts/libsodium_build.sh,
then libsodium_make.sh.

```
# in build/scratch/vend/src/libsodium:
find . -type f -exec touch -d '2020-01-01 00:00:00' {} +
touch -d '2021-01-01 00:00:00' configure $(find . -name 'Makefile.in')

# in build/scratch/vend/build/libsodium:
MAKE=mingw32-make ../../src/libsodium/configure \
  --host=x86_64-w64-mingw32 \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  --prefix=C:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/libsodium \
  CFLAGS='-O2'

mingw32-make -j8 "SHELL=C:/PROGRA~1/Git/usr/bin/sh.exe"
mingw32-make "SHELL=C:/PROGRA~1/Git/usr/bin/sh.exe" install
```

Why the oddities (all required, each caused a real failure first):
- A git checkout has no meaningful mtime ordering, so autotools would try to
  re-run automake/autoconf; sources are backdated, then configure and all
  Makefile.in files are touched forward.
- configure is invoked by relative path because native mingw32-make cannot
  stat MSYS-style absolute /c/... srcdir paths.
- SHELL is passed on the make command line because Git Bash exports
  SHELL="C:/Program Files/Git/usr/bin/sh.exe" and GNU make prefers that
  unquoted path value over the Makefile value; the space-free 8.3 path fixes it.

### libuv v1.53.0 (CMake + Ninja)

```
cmake -G Ninja -S C:/Users/Krishnesh/dev/cp3/build/scratch/vend/src/libuv -B C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/libuv -DCMAKE_BUILD_TYPE=Release -DLIBUV_BUILD_SHARED=OFF -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF -DCMAKE_INSTALL_PREFIX=C:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/libuv
cmake --build C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/libuv --parallel 8
cmake --install C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/libuv
```

Verified: the configure line reproduces the delivered build/libuv/CMakeCache.txt
byte-for-byte (modulo the build directory path) in a fresh directory, and
--build/--install re-run green (ninja: no work to do / all files up-to-date).

### oneTBB v2023.1.0 (CMake + Ninja, static, tests off)

```
cmake -G Ninja -S C:/Users/Krishnesh/dev/cp3/build/scratch/vend/src/oneTBB -B C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/oneTBB -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DTBB_TEST=OFF -DCMAKE_INSTALL_PREFIX=C:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/oneTBB
cmake --build C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/oneTBB --parallel 8
cmake --install C:/Users/Krishnesh/dev/cp3/build/scratch/vend/build/oneTBB
```

Same configure-reproduction and --build/--install verification as libuv.
Upstream emits two warnings on this configuration (expected, quoted verbatim):
- CMakeLists.txt:141: "Dependency signature verification during dynamic
  linking in run-time is disabled by default.  This may lead to security
  vulnerabilities."
- CMakeLists.txt:160: "You are building oneTBB as a static library.  This is
  highly discouraged and such configuration is not supported.  Consider
  building a dynamic library to avoid unforeseen issues."
Also: "TBBBind build targets are disabled due to unsupported environment"
(no hwloc on MinGW). See Risks in the wave report.

### uSockets (part of uWebSockets v20.80.0)

uWebSockets v20.80.0 ships no CMakeLists anywhere; the project's own Makefile
is the only supported build. Run from Git Bash. Script:
build/scratch/vend/scripts/usockets_build.sh.

```
# in build/scratch/vend/src/uWebSockets/uSockets:
mingw32-make CC=gcc AR=ar WITH_LIBUV=1 WITH_LTO=0 CFLAGS="-IC:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/libuv/include" "SHELL=C:/PROGRA~1/Git/usr/bin/sh.exe"
```

The Makefile itself then runs (captured in its build.log):

```
gcc -IC:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/libuv/include -DLIBUS_NO_SSL -DLIBUS_USE_LIBUV -std=c11 -Isrc -O3 -c src/*.c src/eventing/*.c src/crypto/*.c src/io_uring/*.c
lib.exe /out:uSockets.a *.o || ar rvs uSockets.a *.o
```

lib.exe is not installed on this machine, so the documented ar fallback ran.
LIBUS_NO_SSL is the Makefile's own default with no SSL backend selected (TLS
off); WITH_LTO=0 because upstream notes LTO is unsupported on Windows.

libuv decision: uSockets v20.80.0 has no libuv submodule (only boringssl and
lsquic are vendored as submodules), so the standalone libuv v1.53.0 archive
above is the single libuv build used everywhere in the tree.

### libuWebSockets.a

uWebSockets is header-only (no .cpp, no library target); a consumer TU that
includes <App.h> ends up with zero undefined uWS:: symbols (verified with nm).
lib/libuWebSockets.a is nevertheless shipped, built from one documented
translation unit, build/scratch/vend/build/uWebSockets/libuWebSockets.cpp
(see its header comment). Script:
build/scratch/vend/scripts/uwebsockets_lib_build.sh.

```
g++ -std=c++20 -O3 -DUWS_NO_ZLIB -IC:/Users/Krishnesh/dev/cp3/third_party/uWebSockets/include -IC:/Users/Krishnesh/dev/cp3/third_party/libuv/include -c libuWebSockets.cpp -o libuWebSockets.o
ar rcs libuWebSockets.a libuWebSockets.o
cp -f libuWebSockets.a C:/Users/Krishnesh/dev/cp3/third_party/uWebSockets/lib/libuWebSockets.a
```

Every symbol it can define is inline/template (weak COMDAT), so the linker
never pulls it out unless a consumer has an undefined uWS symbol, which cannot
happen with these headers. Linking -luWebSockets is optional and safe.

### PCRE2 10.49 (CMake + Ninja, static 8-bit only)

The core regex backend (DECISIONS D48/D70). Verbatim scripts:
build/scratch/vend/scripts/pcre2_build.ps1 (configure/build/install) and
pcre2_vendor_copy.ps1 (populate third_party/pcre2/ + write VERSION.txt).
Effective commands, with src/build/install under build/scratch/vend/:

```
git clone --depth 1 --branch pcre2-10.49 https://github.com/PCRE2Project/pcre2.git src/pcre2
git -C src/pcre2 checkout --detach 6f9d7c1373262c541324a16a358785b33ef116cf

cmake -G Ninja -S src/pcre2 -B build/pcre2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DPCRE2_BUILD_PCRE2_8=ON -DPCRE2_BUILD_PCRE2_16=OFF -DPCRE2_BUILD_PCRE2_32=OFF \
  -DPCRE2_BUILD_TESTS=OFF -DPCRE2_BUILD_PCRE2GREP=OFF \
  -DPCRE2_SUPPORT_JIT=OFF -DPCRE2_STATIC_PIC=OFF \
  -DCMAKE_INSTALL_PREFIX=install/pcre2
cmake --build build/pcre2 --parallel 4
cmake --install build/pcre2
```

Flag notes (all verified in the delivered build/scratch/vend/build/pcre2/
CMakeCache.txt):
- 8-bit width only, static only: the wrapper binds the 8-bit API; 16/32-bit
  widths and the shared library would be dead weight (D70 ships static only).
- JIT off: M1 runs on the interpreter; turn it back on only if regex
  throughput ever matters (sljit is vendored in the source but not built).
- PCRE2_STATIC_PIC=OFF: the archive is linked into static programs.
- Unicode support is the upstream default (PCRE2_SUPPORT_UNICODE:BOOL=ON);
  the JS `u`/`v` flags map to PCRE2_UTF on top of it.
- cmake 4.3.2 configured PCRE2 10.49 with no
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5, like the other CMake projects here.

## Verified consumer flags (handoff for cp3c)

| family | compile | link |
| --- | --- | --- |
| std/crypto (libsodium) | -I third_party/libsodium/include | -L third_party/libsodium/lib -lsodium |
| std/network (libuv) | -I third_party/libuv/include | -L third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization |
| std/network (uWebSockets + uSockets) | -std=c++20 -DUWS_NO_ZLIB -I third_party/uWebSockets/include -I third_party/libuv/include | -L third_party/uWebSockets/lib -luSockets -L third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization (add -luWebSockets if desired; optional) |
| parallel.* (oneTBB) | -std=c++20 -I third_party/oneTBB/include | -L third_party/oneTBB/lib -ltbb (add -ltbbmalloc for tbb::scalable_allocator) |
| core/regex (PCRE2, static 8-bit) | -DPCRE2_STATIC=1 -DPCRE2_CODE_UNIT_WIDTH=8 -I third_party/pcre2/include | -L third_party/pcre2/lib -lpcre2-8 |

Notes:
- third_party/uWebSockets/include is ONE include root that serves both
  #include <App.h> and #include <libusockets.h> (uWebSockets src/*.h plus
  uSockets src/*.h plus the internal/ subtree, flattened). The extra
  -I third_party/libuv/include is required because the uSockets headers include
  <uv.h> via internal/eventing/libuv.h when built with LIBUS_USE_LIBUV.
- UWS_NO_ZLIB must be defined by consumers; the archives are built with
  compression off and there is no zlib in the tree.
- PCRE2 needs -DPCRE2_STATIC=1 -DPCRE2_CODE_UNIT_WIDTH=8 at the include site
  of pcre2.h (src/runtime/src/core/regex.cpp is the only TU that includes it).
  Both build paths supply them: src/runtime/CMakeLists.txt for the cp3_runtime
  library, and src/cli/src/detail/native_regex.hpp for the per-program compile
  the CLI drives (the regex wave's pcre2-link.txt evidence shows
  "-O2 -DPCRE2_STATIC=1 -DPCRE2_CODE_UNIT_WIDTH=8" on that compile line).
- The 6 Windows system libraries for libuv/uSockets were each discovered from
  real ld undefined-reference errors, not guessed. Symbol -> library:
  WSAGetLastError, WSASocketW, closesocket, htonl, WSAIoctl -> ws2_32;
  GetUserProfileDirectoryW -> userenv; GetAdaptersAddresses -> iphlpapi;
  CoTaskMemFree -> ole32; SymGetOptions, SymSetOptions, MiniDumpWriteDump ->
  dbghelp; WaitOnAddress, WakeByAddressSingle -> synchronization.
  The list is necessary, not merely sufficient for whole-archive: dropping
  -lole32 -ldbghelp from the reference uv smoke link reproduces undefined
  references to __imp_CoTaskMemFree, __imp_SymSetOptions and MiniDumpWriteDump
  (log: build/scratch/vend/smoke/uv/minimal_link_check.txt), and a
  -Wl,--whole-archive -luv -Wl,--no-whole-archive link, which pulls every
  libuv object, also links and runs clean with the full list
  (log: build/scratch/vend/smoke/uv/whole_archive_check.txt).
- All flags above were verified with -static -static-libgcc -static-libstdc++
  as well; the fully static smoke executables run with PATH scrubbed to
  C:\Windows\System32 (evidence: build/scratch/vend/smoke/STATIC-VERIFY.txt).

## Caveats

1. libsodium AES-256-GCM: in this build
   crypto_aead_aes256gcm_is_available() returns 0 even though the smoke
   encrypt/decrypt round-trip succeeds (CPU reports aesni=1 pclmul=1).
   Upstream configure.ac rejects the AVX/XGETBV asm path for Windows targets
   ("#  error Windows x86_64 calling conventions are not supported yet"), so
   the compile-time AVX detection stays off. cp3c should not gate AES-256-GCM
   use on is_available() alone; XChaCha20-Poly1305/secretbox is the safe
   default, and crypto_aead_aes256gcm_encrypt itself demonstrably works here.
2. oneTBB static: upstream calls this configuration "highly discouraged and
   such configuration is not supported" (warning at CMakeLists.txt:160). It
   built and the parallel_for smoke passes, but treat it as a known risk.
   TBBBind (NUMA binding, needs hwloc) is disabled on this platform.
3. TLS and compression are off by design: uSockets is LIBUS_NO_SSL, consumers
   define UWS_NO_ZLIB. If cp3 later needs wss:// or per-message deflate, the
   uSockets/uWebSockets archives must be rebuilt with an SSL backend (and zlib).
4. libtbb12.a is copied as libtbb.a purely so consumers write -ltbb; the
   archive bytes are upstream's.
5. PCRE2: only the 8-bit width is shipped and only libpcre2-8.a (the include
   tree also carries pcre2posix.h, but the POSIX archive is not delivered);
   JIT is off, so matching runs on the interpreter. Unicode support is on
   (upstream default). See the flag notes in the PCRE2 build section.
6. See also: build/scratch/vend/WAVE-VEND-REPORT.md for the full wave report,
   issues and handoff.

## Ledger F — internal-only libraries (VENDORED 2026-11-05, per lead decisions)

> Status: 5 vendored (unordered_dense, fast_float, simdutf, moodycamel, XXHash).
> No compress/miniz (lead: no std/compress lib), no fmt (logger stays hand-written),
> no Boost.Lockfree (moodycamel chosen), no wyhash (XXH3 chosen), CRC32C hand-rolled stays.
> Pins below are real (cloned at tags); VERSION.txt written; windows-x64 archives built
> for compiled libs (xxhash, simdutf); linux-x64/arm64 archives follow the same single-TU
> pattern (documented in build.ps1 pins). Detail: docs/design/internal-libs.md.

| library | upstream (candidate) | kind | internal hot path (current stand-in) | status |
| --- | --- | --- | --- | --- |
| ankerl::unordered_dense | https://github.com/martinus/unordered_dense.git v4.3.1 7c58a8b7a48e790fe72f6508eb3b46317cbba9d5 | header-only (MIT) | `std/collections` Map/Set, `std/io` fd table | VENDORED (headers) |
| fast_float | https://github.com/lemire/fast_float.git master 6a3fabc342d4d149722fc2316babdcf24fdf8fb9 | header-only (Apache-2.0) | `std/json`/`std/env`/`std/url` number parse | VENDORED (headers) |
| simdutf | https://github.com/simdutf/simdutf.git v6.1.0 7852150e454ebacbf7e65cbf2c5e3feac5e660ec | compiled static (Apache-2.0) | `std/text` decode, `std/string` `charAt` | VENDORED (windows-x64 .a) |
| moodycamel concurrentqueue | https://github.com/cameron314/concurrentqueue.git v1.0.4 6dd38b8a1dbaa7863aa907045f32308a56a6ff5d | header-only (BSD-2-Clause) | `Channel`, async log sink | VENDORED (headers) |
| XXHash (XXH32/64/3) | https://github.com/Cyan4973/xxHash.git v0.8.3 e626a72bc2321cd320e953a0ccf1584cad60f363 | compiled static (BSD-2-Clause) | `std/hash` xxhash64, Map/Set bucket (XXH3) | VENDORED (windows-x64 .a) |
| miniz / fmt / Boost.Lockfree / wyhash / CRC32C | — | — | — | DEFERRED per lead (no compress, logger stays, moodycamel over Boost, XXH3 over wyhash, CRC32C hand-rolled) |
| libsodium seed / CSPRNG | already vendored (see Pins) | compiled static (ISC) | `std/crypto` `randombytes_buf`/`sodium_init` — PRESENT, nothing to add | PRESENT |

### Wiring notes for the lead (do not land until vendored)

Header-only (include dir only, no archive):

```cmake
# cp3_runtime (src/runtime/CMakeLists.txt), PRIVATE
target_include_directories(cp3_runtime PRIVATE
    "../../third_party/unordered_dense/include"   # ankerl::unordered_dense
    "../../third_party/fast_float/include"        # fast_float (fast_float.h)
    "../../third_party/concurrentqueue/include"   # moodycamel OR boost lockfree, not both yet
    # "../../third_party/boost/include"           # if Boost.Lockfree chosen instead
    "../../third_party/wyhash/include"            # if wyhash chosen for bucket hash
)
```

Compiled static (archive per-triple like libsodium/libuv, `native.hpp` row):

```
fmt:        -I third_party/fmt/include                  -L third_party/fmt/lib/<triple> -lfmt
            (or -DFMT_HEADER_ONLY, fmt/include only — decide once; header-only inflates TU count)
simdutf:    -I third_party/simdutf/include              -L third_party/simdutf/lib/<triple> -lsimdutf
miniz:      -I third_party/miniz/include                -L third_party/miniz/lib/<triple> -lminiz
xxhash:     -I third_party/xxhash/include               -L third_party/xxhash/lib/<triple> -lxxhash
crc32c:     (only if HW-accel wanted) -I third_party/crc32c/include -L third_party/crc32c/lib/<triple> -lcrc32c
```

Per-family first consumers (after vendoring): unordered_dense ->
`src/std/collections/include/cp3/std/collections.hpp` Map/Set index;
fast_float -> `src/std/json/include/cp3/std/json_parse.hpp` number parse
(then env/url); simdutf -> `src/std/text` decode + `std/string` charAt;
fmt -> `src/std/log/include/cp3/std/log.hpp` render + console;
xxhash/wyhash -> `collections.hpp` KeyHash and unblock `std/hash` xxhash64;
miniz -> new `src/std/compress/**` (family does not exist yet);
moodycamel/Boost.Lockfree -> new Channel + async log sink (neither exists yet).
libsodium: no change.

## Reproducing

Run third_party/build.ps1. It clones the pinned sources into
build/scratch/vend/src, builds into build/scratch/vend/build, installs into
build/scratch/vend/install, populates third_party/ (PCRE2 via its own
pcre2_build.ps1 + pcre2_vendor_copy.ps1 steps), and re-runs the smoke
matrix (use -SkipSmokes to stop before the smokes). Requirements: git, gcc,
g++, cmake, ninja, mingw32-make, ar on PATH, plus Git Bash at
C:\Program Files\Git\bin\bash.exe (the bash.exe in C:\Windows\system32 is WSL
and is not used).
