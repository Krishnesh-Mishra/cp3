---
title: Runtime libraries
---
# Runtime libraries

CP3 does not reinvent the hot paths, the parsers, or the protocol stacks. Every hard problem in a systems language — TLS, crypto, HTTP parsing, regex, JSON parsing and serialization, UTF-8 validation, concurrent maps — is handed to a **pinned, static-vendored, in-git** native library that the generated executable links directly. Nothing is fetched at build time, nothing depends on the user's system libraries, and the same bytes ship in every installer.

This chapter lists what CP3 actually uses, what it deliberately does not, and how the pieces are wired.

## The vendoring model

- All prebuilt dependencies live in `vendor/third_party/<lib>/`, pinned by version + commit SHA + SHA-256 in `vendor/third_party/MANIFEST.md`. Archives per target platform triple (`windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`) are committed to the repository — `vendor/third_party/build.ps1` rebuilds them from source.
- Generated programs link the vendored archives **statically at program link time** (D70). The compiler binary `cp3c` itself links none of them.
- Installers stage only the `third_party` trees that some native row references — nothing else is shipped.
- There is no `FetchContent`, no git submodules, no system-library discovery: reproducible bits are the contract.

## What CP3 uses, and why

| Library | Pinned version | License | What it powers |
| --- | --- | --- | --- |
| **libuv** | v1.53.0 | MIT | The I/O reactor. Backing the loop for `std/io` (fd readiness), `std/network` (TCP/UDP/DNS/fetch transport, timers via the shared loop), `std/fs`, `std/os`, `std/process`, `std/readline`, and `std/json`'s file API. One per-thread loop, subordinate to CP3's own `EventLoop` — see [Async non-blocking I/O](./04-async-non-blocking-io.md). |
| **uWebSockets + uSockets** | v20.80.0 | Apache-2.0 | The HTTP **and** WebSocket server: routing, HTTP/1.1 parsing, WS handshake and framing — all inside the same libuv loop, in-process. Built `WITH_OPENSSL=1` for TLS (`uWS::SSLApp`). `UWS_NO_ZLIB` everywhere — there is deliberately no zlib in the tree. This is the engine behind the `health-server` benchmark numbers. |
| **OpenSSL** | 3.5.5 | Apache-2.0 | TLS everywhere: `https://` fetch, `wss://` WebSockets, `createServer({ tls })`, `createTlsServer`. Wired as a **memory-BIO** engine — libuv owns the socket, ciphertext flows between the two, OpenSSL never sees an fd. Includes an embedded Mozilla CA bundle (no runtime file lookup), SNI, hostname verification, TLS 1.2 floor, ALPN, handshake timeouts, and a per-server handshake rate limiter. uWS's TLS path uses the same vendored OpenSSL build. |
| **PCRE2** | 10.49 | BSD-3-Clause | The `RegExp` backend and regex routing in the webserver. |
| **libsodium** | 1.0.22 | ISC | `std/crypto`: `randomBytes`, `randomUUID` (v4), `sha256`/`sha512`, HMAC, `blake2b`, and the AES-256-GCM envelope (nonce ‖ ciphertext ‖ tag, base64; tampering throws a catchable `Thrown`). Also seeds the Map/Set bucket hash. |
| **xxHash** | v0.8.3 | BSD-2-Clause | `std/hash`'s `xxhash64`, and the XXH3 seed-based bucket hash behind `Map`/`Set` iteration and lookup. Non-cryptographic, chosen for raw throughput. |
| **ankerl::unordered_dense** | v5.3.3 | MIT | The `Map`/`Set`/`LRUCache`/`BitSet`/`MultiMap` backing store - dense open-addressing tables, header-only. `Queue` moved to the moodycamel concurrentqueue in the v0.2.6 change set; everything else on this row stays put. This is why CP3's maps outrun `std::unordered_map` in front of your data. |
| **fast_float** | pinned `master` @ `6a3fabc3` | Apache-2.0 | JSON number parsing in `std/json`'s reader as it stands today. In the v0.2.6 change set the `std/json` parse backend is simdjson (row below), which owns the scanner and the number bytes; no other in-tree consumer exists (`std/env` and `std/url` never moved onto it). |
| **simdjson** | v5.1.1 | Apache-2.0 | The `std/json` **parse** backend in the v0.2.6 change set - `JSON.parse<T>`, `parseFile`/`parseFileSync`, the file-API read path, and the number/scanner. The engine decides the scanner cursor; the hand-written record/array builders consume it. Public API and diagnostics are unchanged. Vendored as a single amalgamated translation unit (`vendor/third_party/simdjson/include/simdjson.h` + `src/simdjson.cpp`) compiled into the consuming program - no archive. |
| **glaze** | v9.1.0 | MIT | The `std/json` **stringify** backend in the v0.2.6 change set - `JSON.stringify`, `stringifyToFile`/`stringifyToFileSync`. JSON serialization only; glaze's CSV/CBOR/BSON and every non-JSON serializer are explicitly excluded. Header-only (`vendor/third_party/glaze/include/glaze/`); templates instantiate on demand - nothing to link. Public API unchanged. |
| **simdutf** | v6.1.0 | Apache-2.0 / MIT | UTF-8 validation in `std/text`'s `TextDecoder.decode` — validated bytes, not hoped-for bytes. |
| **oneTBB** | v2023.1.0 | Apache-2.0 | The scheduler behind `parallel.*`'s numeric kernels (`parallel.for`, `parallel.reduce`). A static `libtbb.a` linked only for programs that use `parallel.*` - core language, no import - via the `core/parallel` native row: generated C++ naming `cp3/parallel/parallel.hpp` selects it (the same content-marker selection as the regex and collections core rows). Headers at `vendor/third_party/oneTBB/include`; windows-x64 archive at `vendor/third_party/oneTBB/lib/libtbb.a`; the linux-x64 archive is being staged at `vendor/third_party/oneTBB/lib/linux-x64` by the maintainer in the same change set (until it lands, a linux-x64 link using the parallel family fails closed with a clear diagnostic). Induction-variable widths and the first-error-wins error propagation are unchanged; the worker/Channel message-passing layer (FIFO, drop-on-close) keeps its existing mechanism. |
| **moodycamel concurrentqueue** | v1.0.4 | BSD-2-Clause | Re-backs `std/collections`' `Queue<T>` only, in the v0.2.6 change set - exact size semantics via atomic counters (not moodycamel's `size_approx`), with `peek()` served from a cached head slot. Every other collection keeps its current backing (unordered_dense, vectors, heaps). Header-only (`vendor/third_party/moodycamel/`). Public API unchanged. |
| **clang-c headers** | LLVM `llvmorg-23.1.0` | Apache-2.0 (LLVM exception) | Compile-time only: `cp3c native-install` can parse C/C++ headers without an LLVM dev install. libclang itself is `LoadLibrary`'d from the *user's* LLVM at runtime — it is not vendored and not linked. |

## What CP3 does not use (and why)

| Library | Pinned | Status | The reason |
| --- | --- | --- | --- |
| **llhttp / http-parser / nghttp** | — | Not vendored | Server-side HTTP parsing is uWS's built-in parser; fetch parsing is hand-written and self-contained. |
| **nlohmann/json, fmt, spdlog, Boost** | — | Not present | No comfort headers, no logging facade, no grab-bag — the runtime is exactly the surface above. |
| **mbedTLS / BoringSSL** | — | Rejected | OpenSSL's stable C ABI and uWS's native integration won; mbedTLS was the fallback if OpenSSL had failed to vendor. |
| **zlib** | — | Not in the tree | `UWS_NO_ZLIB` everywhere; HTTP compression is off by design. |

## How the pieces are wired

Every family with a native backend owns one row of build wiring in its own `native.hpp` (e.g. `src/std/network/codegen/native.hpp`): the include dirs, the defines, the runtime translation units to compile, the library directories, and the libraries to link — for example, `std/network` contributes `-DUWS_NO_ZLIB -DCP3_TLS_OPENSSL -DCP3_UWS_SSL`, compiles the marked runtime TUs, and links `uSockets`, `uv`, `ssl`/`crypto`, `sodium`, `xxhash`, and the platform sockets libraries on Windows.

In the v0.2.6 change set, the JSON, collections and parallel backends ride the same machinery:

- Header-only include-only libraries (unordered_dense, fast_float, moodycamel concurrentqueue, glaze) flow through the runtime mirror include roots the same way fast_float is consumed today - there is no archive to link, and their templates instantiate on demand in the consuming TUs. glaze's `glaze/csv` and every non-JSON serializer are not included anywhere.
- simdjson adds no archive either: its amalgamated `simdjson.cpp` is compiled as one additional translation unit into the consuming program.
- oneTBB links the way libuv does for network programs - only for programs that need it, never otherwise: `parallel.*` is core language (no import), so the `core/parallel` native row is selected by a content marker in the generated C++ (it names `cp3/parallel/parallel.hpp`), exactly like the regex and collections core rows. The row supplies the oneTBB include root and the `-ltbb` link line (windows-x64: `vendor/third_party/oneTBB/lib`; the linux-x64 archive is being staged at `vendor/third_party/oneTBB/lib/linux-x64` in the same change set). A program that only uses `new Worker(...)` never names that header and links no TBB.

The build selects rows **by your import graph** (plus a few content markers for core features like regex literals, the collections runtime, and `parallel.*`): a program that never imports `std/network` compiles and links **none** of the network backend, and no unused archive is pulled in. `--show-runtime` shows you exactly which TUs and headers your program was built against. Marker-gated selection means the cost table is the import table.

## Why so few dependencies?

Every dependency is code that ships into every user binary and every audit. CP3's answer is to ship exactly the ones that are effectively unforkable — the TLS stack, the reactor, the crypto substrate, the regex engine, the JSON scanner and serializer, the parallel scheduler — and nothing that can be done better in-house with less surface. That is the "bug-proof and powerful" story in one sentence: entire classes of vulnerabilities live in code CP3 never wrote.

One caveat to hold in mind: `fast_float` is pinned to a `master` commit rather than a tag, the vendored OpenSSL tree carries no `LICENSE` file of its own (Apache-2.0 is recorded in `VERSION.txt` and the manifest), and oneTBB is linked as a static archive even though upstream labels that configuration unsupported. All three are tracked in the vendoring manifest; none of them changes what your program links.

## Related pages

- [Async non-blocking I/O](./04-async-non-blocking-io.md) — the loop these libraries ride on
- [No garbage collector](./02-no-garbage-collector.md) — lifetimes in the runtime these libraries inhabit
- [Compiler design](./01-compiler-design.md) — where the link step sits in the pipeline
- [Native interop](../cli-and-compiler/07-native-interop.md) — the foreign-function boundary, for when you need one more library
