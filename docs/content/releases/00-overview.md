---
title: Releases
---
# Releases

Release notes for published CP3 compiler builds. Every release lists its download table, highlights, the changes since the previous release, and the known issues that shipped. CP3 is under active development — check the release notes before comparing behavior across versions.

## Current

- **[v0.2.4 — automatic cycle reclamation, typed events, module boundaries](./v0.2.4.md)** — the current release. Strong reference cycles among cycle-capable classes are reclaimed by the trial-deletion collector; `EventEmitter<Events>` is first-class; imports of classes, interfaces and enums across modules now work; TypeScript-grade control-flow narrowing. Ships ten post-release fixes.

## Previous

- [v0.2.2 — compiler fixes and open spec questions resolved](./v0.2.2.md) — async adoption, `parseArray` semantics, worker error propagation, and the closed-literal union fixes.
- [v0.2.1 — networking and release tooling](./v0.2.1.md) — raw TCP/UDP/TLS sockets exported from `std/network`, ARM64 TLS status, `.cp3pak` cache rework.
- [v0.2.0 — TypeScript compatibility](./v0.2.md) — the v0.2 series baseline: typed Promise handlers, native integer types, `parallel.*` diagnostics, fresh-literal `Object.assign` merge semantics, virtual dispatch.

See the [repository's Releases page](https://github.com/Krishnesh-Mishra/cp3/releases) for installers and SHA256SUMS.
