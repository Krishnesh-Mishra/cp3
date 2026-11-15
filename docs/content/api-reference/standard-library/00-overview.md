---
title: Standard library APIs
---
# Standard library APIs

Signature-level pages for every `std/...` module. Each page lists the import form, the exported surface, and per-member signatures — the dictionary, not the tutorial.

| Module | Provides |
| --- | --- |
| [Console](./console.md) | `console.log`, timers, tables, input |
| [Environment](./env.md) | `.env` loading and typed lookups |
| [Process](./process.md) | exit, argv, env, child processes, signals, stdio |
| [File system](./fs.md) | sync and promise-based file and directory operations, `fs.watch` |
| [Path](./path.md) | platform-aware path manipulation |
| [Operating system](./os.md) | host, machine, user and temp information |
| [Math](./math.md) | constants and functions |
| [Date](./date.md) | UTC-millisecond `Date` operations |
| [JSON](./json.md) | checked parsing, stringify, `parseFile`, `verify` |
| [Cryptography](./crypto.md) | random bytes, UUIDs, hashes, HMAC, AES-256-GCM |
| [Buffer](./buffer.md) | byte buffers and text encodings |
| [Collections](./collections.md) | `Map`, `Set`, `LRUCache`, queues, stacks, and friends (ambient, no import needed) |
| [Network](./network.md) | `fetch`, HTTP server, WebSockets, raw TCP/UDP/TLS sockets |
| [Typed arrays](./typed-arrays.md) | fixed-width arrays, buffers, `DataView` |
| [Regular expressions](./regular-expressions.md) | regex literals, methods, flags |
| [I/O readiness](./io.md) | non-blocking fd readiness for native libraries |
| [URL](./url.md) | `URL`, `URLSearchParams`, percent-encoding |
| [Text](./text.md) | `TextEncoder`, `TextDecoder` |
| [Time](./time.md) | monotonic stopwatches, `console.time` |
| [Readline](./readline.md) | line reading over stdin |
| [Hash](./hash.md) | crc32/crc32c/fnv/xxh64 |
| [URL extensions](./url-extensions.md) | base resolution, normalization |

For behavior details and runnable examples, the [guide pages](../standard-library/00-overview.md) are the right place.
