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
build/scratch/vend/scripts/ and vendor/third_party/build.ps1 re-runs the whole flow.

Linux x64 archives are recorded below next to the Windows originals. The
xxhash, simdutf and PCRE2 linux-x64 archives were built on 2026-11-07 in WSL2
Ubuntu (GCC 15.2.0, cmake 4.2.3, GNU ar) with the vendored build scripts; the
libsodium/libuv/uSockets linux-x64 archives were built earlier with the same
toolchain family. linux-arm64 and windows-arm64 archives exist for all seven
vendored libraries (all built 2026-11-08): libsodium, libuv, xxhash, simdutf,
PCRE2 and OpenSSL 3.5.5 for both arm64 targets (windows-arm64 via pinned
llvm-mingw 20261104, linux-arm64 with aarch64-linux-gnu GCC 15.2.0);
uSockets was rebuilt WITH_OPENSSL=1 for all four platforms (TLS-capable
like the windows-x64 and linux-x64 builds).

## Pins

| library | repo URL | tag | commit (full SHA) |
| --- | --- | --- | --- |
| libsodium 1.0.22 | https://github.com/jedisct1/libsodium.git | 1.0.22-RELEASE | 77e1ce5d6dee871c49ef211222ba18ef0c486bda |
| libuv | https://github.com/libuv/libuv.git | v1.53.0 | 840404ce8ba7cc0204be52389a6cfff9f2c90fb6 |
| uWebSockets (incl. uSockets) | https://github.com/uNetworking/uWebSockets.git | v20.80.0 | 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5 |
| oneTBB | https://github.com/uxlfoundation/oneTBB.git | v2023.1.0 | 3046c8b0c29df995980003ea24f4d78c80ec0c8d |
| PCRE2 | https://github.com/PCRE2Project/pcre2.git | pcre2-10.49 | 6f9d7c1373262c541324a16a358785b33ef116cf |
| simdjson | https://github.com/simdjson/simdjson.git | v5.1.1 | 40a686ab89dc5ee58ec10d0514dd5c99abbc472b |

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
| uWebSockets/lib/libuSockets.a | 87038 | 74C01266147AEF7670A0EB88123570CFDEBB434508D9C2988734229724F00293 |
| uWebSockets/lib/windows-x64/libuSockets.a | 87038 | 74C01266147AEF7670A0EB88123570CFDEBB434508D9C2988734229724F00293 |
| uWebSockets/lib/linux-x64/libuSockets.a | 122710 | 06B3144E93C19B3323A931D09FFDFC1134FFEA2A07BD29896AFDF139EA7C60DC |
| uWebSockets/lib/linux-arm64/libuSockets.a | 129758 | 11E13DEF4009B831E64C92CDFD74F6F359E399D75F828E7766932950627A099D |
| simdutf/lib/linux-arm64/libsimdutf.a | 347304 | E9DB567C5C85A037EB4A5D6DD15AB2258C7EEBEA0A173457B12F5CB12B6E2461 |
| xxhash/lib/linux-arm64/libxxhash.a | 75368 | 55FDCE25D000E049CAC4DF8C2E0DFB8F098E8F397F2E5CE0DA1F6797230E113E |
| pcre2/lib/linux-arm64/libpcre2-8.a | 556894 | C1989D9429C61AA614A942FCA483AAE93C9FA918FE20033EB5BA1960990ED6FE |
| openssl/lib/linux-arm64/libssl.a | 2253444 | EDEF2CBB103B2946C6F81A76EB48383EEAAB1272BF4AB9ACB3DC5F0E4CA34D48 |
| openssl/lib/linux-arm64/libcrypto.a | 12382582 | CC9774428E47BFC12C3B36C1021FC60C329D192CE25F4C2F753467B54AB27CD8 |
| libsodium/lib/windows-arm64/libsodium.a | 656564 | 8B6A80F521CBCC7EA98F6A7C75722379FA64F7D0667C5AE77D25BD6C3C0E983B |
| libuv/lib/windows-arm64/libuv.a | 378234 | 5C42DEBD6011EC6EACD410E5756DED1CF41AEF6C7203830EA23753CC673F44FC |
| uWebSockets/lib/windows-arm64/libuSockets.a | 76908 | 3F460E5F342FF2EAF3511926C4364B3B09258B783A0FB4E9956022742D10A972 |
| xxhash/lib/windows-arm64/libxxhash.a | 38050 | 04D2E0134C4028E019DE9E7A567DA0E10945ABC85C9EBF2CD19E8D4590B04793 |
| simdutf/lib/windows-arm64/libsimdutf.a | 268912 | D2F19F4C5D658427E2FE59ED768A585537CB89800AE039374F2C70DCC1BAD1DC |
| pcre2/lib/windows-arm64/libpcre2-8.a | 461700 | F76B342A145F46C6548E6055CC61A5572A37B95E5CF333DE1AB08C8E67E9CAB4 |
| openssl/lib/windows-arm64/libssl.a | 1472650 | FA2B17F26EF9EC662FF4EFDEFB1A839ADE9C5E33E430FFA8BCC8475AAFCF8F7C |
| openssl/lib/windows-arm64/libcrypto.a | 7564256 | 49F108E46728B423FF2CD6A0B8BF73A996C0B0FB91AC374DD771232E0353687E |
| uWebSockets/lib/libuWebSockets.a | 1438 | EB0D1543E48D0F3077BE44104532502AA5C65E8E632564F24973B9384F9ECBF5 |
| pcre2/lib/libpcre2-8.a | 574408 | 79EF3EBB2F088D5FEC2B96035F2EFB173A18109C47523EF7976113ED7AC2E7E4 |
| pcre2/lib/linux-x64/libpcre2-8.a | 635630 | AA643CEEB27B1B5550A6147EF0AB54E58DC33E951491570E8A6D1C6F645A86F6 |
| simdutf/lib/windows-x64/libsimdutf.a | 821310 | 56050F12DCC7A07F627F36101A4213F2404CB6F4E708586201AA217E55D52741 |
| simdutf/lib/linux-x64/libsimdutf.a | 783928 | 27B5E7465DB60E146EC1B037E78F9A78711B44AB840D666AEEE8C3FA2042FE11 |
| xxhash/lib/windows-x64/libxxhash.a | 66088 | 473DA47FD46228A7A2E166078096493AA581B45E46BE9B43E371ED51F7C229DD |
| xxhash/lib/linux-x64/libxxhash.a | 67096 | 3DA25C3EDB7660788D0DE4E6905CD04EFBABFC1F0D6FD57AE6FF19535CB15B83 |

Rebuilds are not guaranteed byte-identical (ar archives record object file
timestamps); the SHA-256 values above identify exactly the delivered bytes.

## Licenses copied into vendor/third_party/

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
`vendor/third_party/clang-c/include` to the include path. The libclang *library* is
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
is the only supported build. The v0.2 TLS bring-up rebuilt the Windows and
linux-x64 archives WITH OpenSSL support (WITH_OPENSSL=1), and linux-arm64 was
rebuilt the same way on 2026-11-08 (aarch64 cross GCC, WITH_OPENSSL=1). The
linux-x64 archive was rebuilt again on 2026-11-09 after the 2026-10-31 build
was found to link-fail with slim-LTO members (see the linux-x64 block).

Windows (ucrt64 gcc 16.2.0 Rev6, 2026-11-07):

```
# tree copied from the pinned checkout; the Makefile hardcodes -flto on the
# src/crypto/*.cpp (sni_tree.cpp) compile, which emits symbol-free LTO slim
# objects on Windows. That flag was removed from the local tree before the
# build (reference: build/scratch notes / the v0.2 TLS work).
make CC=gcc CXX=g++ AR=ar WITH_OPENSSL=1 WITH_LIBUV=1 WITH_LTO=0 \
  CFLAGS="-I$REPO/vendor/third_party/libuv/include -I$REPO/vendor/third_party/openssl/include"
# ar fallback ran (lib.exe is not installed); then:
cp uSockets.a vendor/third_party/uWebSockets/lib/windows-x64/libuSockets.a
cp uSockets.a vendor/third_party/uWebSockets/lib/libuSockets.a   # the root copy
```

libs needed at program link time: -lssl -lcrypto (the vendored 3.5.5
archives; see the openssl section) plus the Windows system libs
(-lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization -lcrypt32).
sni_tree.cpp drags in libstdc++ <map>/<memory> symbols; the consumer link
must therefore use the mingw-w64 UCRT C++ runtime that the uSockets archive
was built against (verified end-to-end: https fetch links and runs).

linux-x64 (gcc 15.2.0, WSL2 Ubuntu, archive rebuilt 2026-11-09): the same
configure line with WITH_OPENSSL=1 WITH_LIBUV=1 WITH_LTO=0, with the
Makefile's hardcoded -flto stripped first (parity with the windows-x64 and
linux-arm64 recipes). The 2026-10-31 archive shipped slim-LTO `sni_tree.o`
(.text == 0) that plain GNU ld cannot read: every Linux link of a std/network
program failed with "plugin needed to handle lto object" and undefined
sni_find/sni_add/sni_new/... The rebuilt archive carries regular code
sections in every member and links.

linux-arm64 (aarch64 cross GCC 15.2.0, WSL2 Ubuntu, 2026-11-08): same recipe,
`CC=/usr/bin/aarch64-linux-gnu-gcc CXX=/usr/bin/aarch64-linux-gnu-g++
AR=/usr/bin/aarch64-linux-gnu-ar WITH_OPENSSL=1 WITH_LIBUV=1 WITH_LTO=0
CFLAGS="-Ivendor/third_party/libuv/include -Ivendor/third_party/openssl/include"`;
the Makefile's hardcoded -flto was stripped first (windows-x64 parity). The
prebuilt LIBUS_NO_SSL archive was replaced; the linux-arm64 native row now
links OpenSSL and compiles the TLS backend, like the other two targets.

Original LIBUS_NO_SSL recipe (for reference / linux-arm64):

```
mingw32-make CC=gcc AR=ar WITH_LIBUV=1 WITH_LTO=0 CFLAGS="-IC:/Users/Krishnesh/dev/cp3/build/scratch/vend/install/libuv/include" "SHELL=C:/PROGRA~1/Git/usr/bin/sh.exe"
```

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
g++ -std=c++20 -O3 -DUWS_NO_ZLIB -IC:/Users/Krishnesh/dev/cp3/vendor/third_party/uWebSockets/include -IC:/Users/Krishnesh/dev/cp3/vendor/third_party/libuv/include -c libuWebSockets.cpp -o libuWebSockets.o
ar rcs libuWebSockets.a libuWebSockets.o
cp -f libuWebSockets.a C:/Users/Krishnesh/dev/cp3/vendor/third_party/uWebSockets/lib/libuWebSockets.a
```

Every symbol it can define is inline/template (weak COMDAT), so the linker
never pulls it out unless a consumer has an undefined uWS symbol, which cannot
happen with these headers. Linking -luWebSockets is optional and safe.

### PCRE2 10.49 (CMake + Ninja, static 8-bit only)

The core regex backend (DECISIONS D48/D70). Verbatim scripts:
build/scratch/vend/scripts/pcre2_build.ps1 (configure/build/install) and
pcre2_vendor_copy.ps1 (populate vendor/third_party/pcre2/ + write VERSION.txt).
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
| std/crypto (libsodium) | -I vendor/third_party/libsodium/include | -L vendor/third_party/libsodium/lib -lsodium |
| std/network (libuv) | -I vendor/third_party/libuv/include | -L vendor/third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization |
| std/network (uWebSockets + uSockets) | -std=c++20 -DUWS_NO_ZLIB -I vendor/third_party/uWebSockets/include -I vendor/third_party/libuv/include | -L vendor/third_party/uWebSockets/lib -luSockets -L vendor/third_party/libuv/lib -luv -lws2_32 -luserenv -liphlpapi -lole32 -ldbghelp -lsynchronization (add -luWebSockets if desired; optional) |
| parallel.* (oneTBB) | -std=c++20 -I vendor/third_party/oneTBB/include | -L vendor/third_party/oneTBB/lib -ltbb (add -ltbbmalloc for tbb::scalable_allocator) |
| core/regex (PCRE2, static 8-bit) | -DPCRE2_STATIC=1 -DPCRE2_CODE_UNIT_WIDTH=8 -I vendor/third_party/pcre2/include | -L vendor/third_party/pcre2/lib -lpcre2-8 |

Notes:
- vendor/third_party/uWebSockets/include is ONE include root that serves both
  #include <App.h> and #include <libusockets.h> (uWebSockets src/*.h plus
  uSockets src/*.h plus the internal/ subtree, flattened). The extra
  -I vendor/third_party/libuv/include is required because the uSockets headers include
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
3. The uSockets archives (all three targets) are rebuilt WITH OpenSSL support
   (WITH_OPENSSL=1, see the uSockets section); linux-arm64 was switched over
   on 2026-11-08. Compression (per-message
   deflate) is still off by design; consumers define UWS_NO_ZLIB.
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
> Pins below are real (cloned at tags); VERSION.txt written; archives built
> for compiled libs (xxhash, simdutf) on windows-x64, linux-x64 and linux-arm64.

| library | upstream (candidate) | kind | internal hot path (current stand-in) | status |
| --- | --- | --- | --- | --- |
| ankerl::unordered_dense | https://github.com/martinus/unordered_dense.git v5.3.3 7c58a8b7a48e790fe72f6508eb3b46317cbba9d5 | header-only (MIT) | `std/collections` Map/Set, `std/io` fd table | VENDORED (headers) |
| fast_float | https://github.com/lemire/fast_float.git master 6a3fabc342d4d149722fc2316babdcf24fdf8fb9 | header-only (Apache-2.0) | `std/json`/`std/env`/`std/url` number parse | VENDORED (headers) |
| simdutf | https://github.com/simdutf/simdutf.git v6.1.0 7852150e454ebacbf7e65cbf2c5e3feac5e660ec | compiled static (Apache-2.0) | `std/text` decode, `std/string` `charAt` | VENDORED (windows-x64 + linux-x64 .a) |
| moodycamel concurrentqueue | https://github.com/cameron314/concurrentqueue.git v1.0.4 6dd38b8a1dbaa7863aa907045f32308a56a6ff5d | header-only (BSD-2-Clause) | no consumer yet (`Channel` deferred; `std/log` removed) | VENDORED (headers) |
| XXHash (XXH32/64/3) | https://github.com/Cyan4973/xxHash.git v0.8.3 e626a72bc2321cd320e953a0ccf1584cad60f363 | compiled static (BSD-2-Clause) | `std/hash` xxhash64, Map/Set bucket (XXH3) | VENDORED (windows-x64 + linux-x64 .a) |
| miniz / fmt / Boost.Lockfree / wyhash / CRC32C | — | — | — | DEFERRED per lead (no compress, logger stays, moodycamel over Boost, XXH3 over wyhash, CRC32C hand-rolled) |
| libsodium seed / CSPRNG | already vendored (see Pins) | compiled static (ISC) | `std/crypto` `randombytes_buf`/`sodium_init` — PRESENT, nothing to add | PRESENT |

### Wiring notes for the lead (do not land until vendored)

Header-only (include dir only, no archive):

```cmake
# cp3_runtime (src/runtime/CMakeLists.txt), PRIVATE
target_include_directories(cp3_runtime PRIVATE
    "../../vendor/third_party/unordered_dense/include"   # ankerl::unordered_dense
    "../../vendor/third_party/fast_float/include"        # fast_float (fast_float.h)
    "../../vendor/third_party/concurrentqueue/include"   # moodycamel OR boost lockfree, not both yet
    # "../../vendor/third_party/boost/include"           # if Boost.Lockfree chosen instead
    "../../vendor/third_party/wyhash/include"            # if wyhash chosen for bucket hash
)
```

Compiled static (archive per-triple like libsodium/libuv, `native.hpp` row):

```
fmt:        -I vendor/third_party/fmt/include                  -L vendor/third_party/fmt/lib/<triple> -lfmt
            (or -DFMT_HEADER_ONLY, fmt/include only — decide once; header-only inflates TU count)
simdutf:    -I vendor/third_party/simdutf/include              -L vendor/third_party/simdutf/lib/<triple> -lsimdutf
miniz:      -I vendor/third_party/miniz/include                -L vendor/third_party/miniz/lib/<triple> -lminiz
xxhash:     -I vendor/third_party/xxhash/include               -L vendor/third_party/xxhash/lib/<triple> -lxxhash
crc32c:     (only if HW-accel wanted) -I vendor/third_party/crc32c/include -L vendor/third_party/crc32c/lib/<triple> -lcrc32c
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

## Ledger G — TLS stack (VENDORED 2026-11-07, per lead decisions)

> Status: 2 vendored (OpenSSL static base + uSockets rebuilt with SSL).
> v0.2 scope: fetch('https://'), createServer({tls}), wss://
> Windows x64 ships prebuilt MSYS2 archives; the Linux x64 archive is built
> from the pinned source. Both are wired into the native rows.
> Mozilla's CA bundle (cacert.pem) is planned for cacert.pem support in
> v0.3.0; for v0.2 the OS trust stores on each triple are used by default
> (Linux: /etc/ssl/certs via SSL_CTX_set_default_verify_paths; Windows:
> the CA bundle ships with the vendor archive and SSL_CERT_FILE can
> override; native Windows store access via CryptoAPI is deferred to
> v0.3.0 to avoid an extra dependency).

| library | upstream | kind | internal hot path | status |
| --- | --- | --- | --- | --- |
| OpenSSL (libssl/libcrypto) | https://github.com/openssl/openssl.git **openssl-3.5.5** 67b5686b4419b4cb8caa502711c41815f5279751 (linux), static prebuilt mingw-w64-x86_64-openssl-3.5.4-1-any.pkg.tar.zst (windows) | compiled static (Apache-2.0) | `std/network` fetch/TlsSocket/`createServer{tls}`, `std/network/std/tls` CAs | VENDORED (windows-x64 + linux-x64 .a) |
| uSockets (+uWebSockets) | https://github.com/uNetworking/uWebSockets.git v20.80.0 3ffd6f44c9c3c92c96345d9f96bd01ba9c025ab5 / uSockets 86097c490263ab662d62e8e7b541390bdec7d149 | rebuilt with `WITH_OPENSSL=1` | `std/network/webserver` HttpServer/WebSocket TLS backend | VENDORED (windows-x64 + linux-x64 .a, replaces the LIBUS_NO_SSL builds) |

The exact recipes and the SHAs live in `vendor/third_party/openssl/VERSION.txt`
and `vendor/third_party/uWebSockets/lib/*/VERSION.txt` / SHA256SUMS files.

## Ledger H — JSON speed tier (VENDORED 2026-11-08, per lead decisions)

> Status: 1 vendored (simdjson, single-TU). The std/json family's `parse`
> backend is proposed to route through this; the public API
> (`JSON.parse`/`stringify`/`verify<T>`) stays the same as the current
> hand-written reader.
> Exact behavior contract (current implementation, to be preserved by the
> engine swap):
>   * Extra fields in the JSON input are ignored silently.
>   * Missing REQUIRED field -> throw (catchable via try/catch).
>   * Missing optional / `| undefined` field -> silent, binding is undefined.
>   * Field type mismatch -> throw.
>   * Nested objects recursively use the same rules.
>   * DUPLICATE KEYS -> throw (never last-writer-wins, never first-writer-wins).
>   * Keys are CASE-SENSITIVE; lowering or collapsing them is a bug.

| library | upstream | kind | status |
| --- | --- | --- | --- |
| simdjson v5.1.1 | https://github.com/simdjson/simdjson/releases/download/v5.1.1 | single-TU vendored (`vendor/third_party/simdjson/{include/simdjson.h,src/simdjson.cpp}`; amalgamated pair from the release, Apache-2.0). SHA-256: simdjson.h `f3e5ee...c5fd`, simdjson.cpp `3ea5f5...f4`. Commit `40a686ab89dc5ee58ec10d0514dd5c99abbc472b`. | VENDORED (headers+source); not built into an archive yet (proposed as a single TU in the generated code, like the hand-written parser but in-C++17, or behind a build flag). No API change to the std/json public surface. |

Wiring notes (do NOT land until the builder contract is chosen):

```cmake
# cp3_common or cp3_runtime — include path only for the headers;
# the .cpp is compiled as one translation unit by the *runtime* row
# or by the std/json row (decide when wiring transforms land).
target_include_directories(cp3_runtime PRIVATE
    "../../vendor/third_party/simdjson/include"
)
# vendor/third_party/simdjson/src/simdjson.cpp goes into the runtime snapshot
# as one TU (needs no archive and no per-target multiplication).
```

Constraints recorded for the future engine swap:
   * Public API shape unchanged (`JSON.parse<T>`, `stringify`, `verify<T>`).
   * Apache-2.0 source only; no GPL-only simdjson derivatives.
   * `parseNumber` / `parseRecord` stay hand-written support; the
     engine only decides the scanner's cursor and the bytes of each scalar.

## Reproducing

Run vendor/third_party/build.ps1. It clones the pinned sources into
build/scratch/vend/src, builds into build/scratch/vend/build, installs into
build/scratch/vend/install, populates vendor/third_party/ (PCRE2 via its own
pcre2_build.ps1 + pcre2_vendor_copy.ps1 steps), and re-runs the smoke
matrix (use -SkipSmokes to stop before the smokes). Requirements: git, gcc,
g++, cmake, ninja, mingw32-make, ar on PATH, plus Git Bash at
C:\Program Files\Git\bin\bash.exe (the bash.exe in C:\Windows\system32 is WSL
and is not used).
