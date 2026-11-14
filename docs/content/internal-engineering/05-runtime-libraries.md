---
title: Runtime libraries
---
# Runtime libraries

CP3 does not reinvent the hot paths, the parsers, or the protocol stacks. Every hard problem in a systems language — TLS, crypto, HTTP parsing, regex, JSON numbers, UTF-8 validation, concurrent maps — is handed to a **pinned, static-vendored, in-git** native library that the generated executable links directly. Nothing is fetched at build time, nothing depends on the user's system libraries, and the same bytes ship in every installer.

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
| **ankerl::unordered_dense** | v5.3.3 | MIT | The `Map`/`Set`/`LRUCache`/`Queue`/`BitSet`/`MultiMap` … backing store — dense open-addressing tables, header-only. This is why CP3's maps outrun `std::unordered_map` in front of your data. |
| **fast_float** | pinned `master` @ `6a3fabc3` | Apache-2.0 | JSON number parsing in `std/json`'s header-only reader — the fastest conforming float parse available, without pulling in a full JSON engine. |
| **simdutf** | v6.1.0 | Apache-2.0 / MIT | UTF-8 validation in `std/text`'s `TextDecoder.decode` — validated bytes, not hoped-for bytes. |
| **clang-c headers** | LLVM `llvmorg-23.1.0` | Apache-2.0 (LLVM exception) | Compile-time only: `cp3c native-install` can parse C/C++ headers without an LLVM dev install. libclang itself is `LoadLibrary`'d from the *user's* LLVM at runtime — it is not vendored and not linked. |

## What CP3 does not use (and why)

| Library | Pinned | Status | The reason |
| --- | --- | --- | --- |
| **simdjson** | v5.1.1 | Vendored, **zero references in `src/`** | std/json is a hand-written recursive-descent parser using fast_float. simdjson is staged for the "JSON speed tier" on the roadmap; docs that claim it is active describe the plan, not the code. |
| **oneTBB** | v2023.1.0 | Vendored, **never linked** | `parallel.*` is a hand-rolled fixed `std::thread` pool — no third-party library is allowed in that layer. |
| **moodycamel concurrentqueue** | v1.0.4 | Vendored, **zero references** | Reserved for the planned `Channel` mailbox work; not yet on any generated path. |
| **llhttp / http-parser / nghttp** | — | Not vendored | Server-side HTTP parsing is uWS's built-in parser; fetch parsing is hand-written and self-contained. |
| **nlohmann/json, fmt, spdlog, Boost** | — | Not present | No comfort headers, no logging facade, no grab-bag — the runtime is exactly the surface above. |
| **mbedTLS / BoringSSL** | — | Rejected | OpenSSL's stable C ABI and uWS's native integration won; mbedTLS was the fallback if OpenSSL had failed to vendor. |
| **zlib** | — | Not in the tree | `UWS_NO_ZLIB` everywhere; HTTP compression is off by design. |

## How the pieces are wired

Every family with a native backend owns one row of build wiring in its own `native.hpp` (e.g. `src/std/network/codegen/native.hpp`): the include dirs, the defines, the runtime translation units to compile, the library directories, and the libraries to link — for example, `std/network` contributes `-DUWS_NO_ZLIB -DCP3_TLS_OPENSSL -DCP3_UWS_SSL`, compiles the marked runtime TUs, and links `uSockets`, `uv`, `ssl`/`crypto`, `sodium`, `xxhash`, and the platform sockets libraries on Windows.

The build selects rows **by your import graph** (plus a few content markers for core features like regex literals): a program that never imports `std/network` compiles and links **none** of the network backend, and no unused archive is pulled in. `--show-runtime` shows you exactly which TUs and headers your program was built against. Marker-gated selection means the cost table is the import table.

## Why so few dependencies?

Every dependency is code that ships into every user binary and every audit. CP3's answer is to ship exactly the ones that are effectively unforkable — the TLS stack, the reactor, the crypto substrate, the regex engine — and nothing that can be done better in-house with less surface. That is the "bug-proof and powerful" story in one sentence: entire classes of vulnerabilities live in code CP3 never wrote.

One caveat to hold in mind: `fast_float` is pinned to a `master` commit rather than a tag, and the vendored OpenSSL tree carries no `LICENSE` file of its own (Apache-2.0 is recorded in `VERSION.txt` and the manifest). Both are tracked in the vendoring manifest; neither changes what your program links.

## Related pages

- [Async non-blocking I/O](./04-async-non-blocking-io.md) — the loop these libraries ride on
- [No garbage collector](./02-no-garbage-collector.md) — lifetimes in the runtime these libraries inhabit
- [Compiler design](./01-compiler-design.md) — where the link step sits in the pipeline
- [Native interop](../cli-and-compiler/07-native-interop.md) — the foreign-function boundary, for when you need one more library
