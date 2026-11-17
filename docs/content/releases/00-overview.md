---
title: Releases
---
# Releases

Release notes for published CP3 compiler builds. Every release lists its download table, highlights, the changes since the previous release, and the known issues that shipped. CP3 is under active development — check the release notes before comparing behavior across versions.

## Current

- **[v0.2.6 — native engine backends, type queries, source packages](./v0.2.6.md)** — the current release. Vendored simdjson/glaze/oneTBB/moodycamel behind `JSON`, `parallel.*` and `Queue<T>`; TypeScript type queries and utility types with closed-world diagnostics; `.cp3pak` becomes a source package under `formatVersion` 2; C++23 floor; harness retries only transient failures.

## Previous

- [v0.2.5 — native type widths, references intel, module navigation](./v0.2.5.md) — native integers across the standard-library surfaces typed by `typeSystem`, references / hover / rename through barrels and multi-file workspaces, member completion for imported types, parser diagnostics for empty import specifiers, the interface-dispatch ABI fix, and editor coverage for same-file goto-definitions.
- [v0.2.4 — automatic cycle reclamation, typed events, module boundaries](./v0.2.4.md) — the trial-deletion collector for strong reference cycles; `EventEmitter<Events>`; cross-module imports of classes, interfaces, enums and records; carrier for ten post-release fixes.
- [v0.2.2 — compiler fixes and open spec questions resolved](./v0.2.2.md) — async adoption, `parseArray` semantics, worker error propagation, and the closed-literal union fixes.
- [v0.2.1 — networking and release tooling](./v0.2.1.md) — raw TCP/UDP/TLS sockets exported from `std/network`, ARM64 TLS status, `.cp3pak` cache rework.
- [v0.2.0 — TypeScript compatibility](./v0.2.md) — the v0.2 series baseline: typed Promise handlers, native integer types, `parallel.*` diagnostics, fresh-literal `Object.assign` merge semantics, virtual dispatch.

See the [repository's Releases page](https://github.com/Krishnesh-Mishra/cp3/releases) for installers and SHA256SUMS.
