# third_party MANIFEST

Pinned, static, reproducible vendor builds used by cp3c for the std/crypto,
std/network and parallel.* families.

Built 2026-10-18 on Windows x64 with:
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

Each library directory carries VERSION.txt with
"<repo URL> <tag> <full SHA> <date built>". All four checkout trees are
detached at the pinned commit, and
`git clone --depth 1 --branch <tag> <url>` was verified to reproduce every
pinned SHA exactly (log: build/scratch/vend/verify/clone_check.txt).

## Delivered artifacts (bytes, SHA-256)

| file | bytes | SHA-256 |
| --- | --- | --- |
| libsodium/lib/libsodium.a | 612878 | 53D7D5022EFF7EBED9A64FCF4543EFDCD0D7D09736F4370D9BF4E71D4C79D714 |
| libuv/lib/libuv.a | 422742 | 66B0B14A07C1943A989B0A33FCB749A6B7275654797AF5A916BCA0D695852544 |
| oneTBB/lib/libtbb.a (upstream name libtbb12.a) | 1107372 | 01F8BF7B97AE88C19BCC87B436ABB29C2E339BF6D6D3583BFC315D984A11A15A |
| oneTBB/lib/libtbbmalloc.a | 245472 | 53AC7AAE285488384808E339854F795454046BD345D8B46AAA15AAE0016D08A0 |
| uWebSockets/lib/libuSockets.a | 47432 | 835B8688A8F301533CA7508E309DF2E832D335FFC0FE029BCD0EC3BD87CE5E50 |
| uWebSockets/lib/libuWebSockets.a | 1438 | EB0D1543E48D0F3077BE44104532502AA5C65E8E632564F24973B9384F9ECBF5 |

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

## What got copied

Only what each recipe needs: include/ trees from the upstream install (or from
the upstream source for header-only uWebSockets), lib/*.a, the upstream license
files, and VERSION.txt. Nothing else. One exception worth naming: upstream's
oneTBB install payload includes a C++ module interface file,
include/oneapi/tbb.cppm, so it is part of the delivered include tree.

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
g++ -std=c++20 -O2 -DUWS_NO_ZLIB -IC:/Users/Krishnesh/dev/cp3/third_party/uWebSockets/include -IC:/Users/Krishnesh/dev/cp3/third_party/libuv/include -c libuWebSockets.cpp -o libuWebSockets.o
ar rcs libuWebSockets.a libuWebSockets.o
cp -f libuWebSockets.a C:/Users/Krishnesh/dev/cp3/third_party/uWebSockets/lib/libuWebSockets.a
```

Every symbol it can define is inline/template (weak COMDAT), so the linker
never pulls it out unless a consumer has an undefined uWS symbol, which cannot
happen with these headers. Linking -luWebSockets is optional and safe.

## Verified consumer flags (handoff for cp3c)

| family | compile | link |
| --- | --- | --- |
| std/crypto (libsodium) | -I third_party/libsodium/include | -L third_party/libsodium/lib -lsodium |
| std/network (libuv) | -I third_party/libuv/include | -L third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization |
| std/network (uWebSockets + uSockets) | -std=c++20 -DUWS_NO_ZLIB -I third_party/uWebSockets/include -I third_party/libuv/include | -L third_party/uWebSockets/lib -luSockets -L third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization (add -luWebSockets if desired; optional) |
| parallel.* (oneTBB) | -std=c++20 -I third_party/oneTBB/include | -L third_party/oneTBB/lib -ltbb (add -ltbbmalloc for tbb::scalable_allocator) |

Notes:
- third_party/uWebSockets/include is ONE include root that serves both
  #include <App.h> and #include <libusockets.h> (uWebSockets src/*.h plus
  uSockets src/*.h plus the internal/ subtree, flattened). The extra
  -I third_party/libuv/include is required because the uSockets headers include
  <uv.h> via internal/eventing/libuv.h when built with LIBUS_USE_LIBUV.
- UWS_NO_ZLIB must be defined by consumers; the archives are built with
  compression off and there is no zlib in the tree.
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
5. See also: build/scratch/vend/WAVE-VEND-REPORT.md for the full wave report,
   issues and handoff.

## Reproducing

Run third_party/build.ps1. It clones the pinned sources into
build/scratch/vend/src, builds into build/scratch/vend/build, installs into
build/scratch/vend/install, populates third_party/, and re-runs the smoke
matrix (use -SkipSmokes to stop before the smokes). Requirements: git, gcc,
g++, cmake, ninja, mingw32-make, ar on PATH, plus Git Bash at
C:\Program Files\Git\bin\bash.exe (the bash.exe in C:\Windows\system32 is WSL
and is not used).
