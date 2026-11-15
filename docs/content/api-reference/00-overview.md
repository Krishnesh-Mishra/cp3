---
title: API reference
---
# API reference

Signature-level reference for everything CP3 exposes — no prose, just the surface. If you want the guides, start from the **Docs** tab; this section is the dictionary you keep open while you work.

## The three areas

| Area | What's inside |
| --- | --- |
| [Language APIs](./language-apis/00-overview.md) | The built-in value APIs and global functions — `String`, `Number`, `Array`, `Object`, typed arrays, `Promise`, `Map`/`Set`, `EventEmitter`, `parallel.*`, and the rest. No import required. |
| [Standard library APIs](./standard-library/00-overview.md) | The `std/...` module surface: every import form, exported value, and signature, one page per module. |
| [Error codes](./error-codes/00-overview.md) | Every `CP`-numbered diagnostic the compiler can print — what triggers it, what happens, and how to fix it. One page per code, with a minimal reproducer. |

Every page in this section lists the surface as it exists in the current compiler, not as it was documented at some earlier release. When the code and a page disagree, the code wins — please report the difference.
