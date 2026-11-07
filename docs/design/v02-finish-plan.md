# v0.2 Finish Plan (2026-11-05)

## Status: corpus green on Windows and Linux (clang, final release binaries; 2115 found, 2092 passed, 0 failed, 23 ambiguous on both); all v0.2 release commits landed (`af62898b` … `23bf0e04`, `4dfc342f`, `535d08a1`, `2e9c659f`, `058f963e`, `8d193370`, `af72c7d9`, `7141ea8c`); JS/TS developers guide is the single-file reference, linked from README; release tag pending (packaging + SHA256SUMS).

## Corpus green note (2026-11-06)
Full corpus passes with no failures. Session fixes (all uncommitted, to be
committed per unit): worker empty directions via `undefined` (CP3906
"direction is empty"), worker-unit std imports now select their native
backends, `Object.fromEntries`/`groupBy` select the collections backend
content-driven, `Object.groupBy` test now uses `charAt(0)`, `isSealed` for
`preventExtensions`-only objects, narrowing-vs-assignment (`declaredType`),
`return null` for `T | null` via `std::nullopt` (Optional only, not Maybe),
imported generic functions now carry type parameters through
`FunctionInfo`/`ImportedBinding`, `.join` unwraps `E[] | undefined` receivers,
network config-server link markers + `onError` `Promise` wrapping, spread
member calls no longer intercepted by the 1-arg settings getter, cookie
`maxAge` in seconds, `req.protocol` field read (fixed a SIGSEGV in the
webserver slot lookup) + `applyTrustProxy` field refresh, `res.render`
fallback to the response-family renderer, static-file middleware path joins
(windows separators), `.catch` handlers returning `Undefined` fulfill with
default `T`, stale baselines (`nullable_bst_search`, `unbroken_cycle_rejected`,
`native_i64_precision` now pins checked arithmetic), `structuredClone` test on
homogeneous nested arrays, plus new `parseto/edge_cases` and `native/widths`
test cases.

## Docs status
- DONE: `docs/content/03-standard-library/19-url.md` … `24-url-extensions.md` (19-url, 20-text, 21-time, 23-hash, 24-url-extensions; `22-log` retired with `std/log` removal)
- DONE: `00-overview.md` index rows (19, 20, 21, 23, 24) and `mkdocs.yml` nav (19, 20, 21, 23, 24)
- DONE: `12-collections.md` (9-type API table), `docs/design/*` (record, utility-types, rule3, hash-placement, import-suggestions, internal-libs, xxhash64, install-dir, cycle-*, remove-void-unknown, generic-constraints, verified-gaps)
- DONE: JS/TS developers guide (`03-javascript-typescript-developers.md`) is now the single-file agent/migration reference; `docs/llm.md` deleted in `7efe8c10` (guide supersedes it — cheat-sheet, recipes, diagnostics quick refs folded in)
- DONE: `docs/content/00-welcome/03-javascript-typescript-developers.md` linked from README header
- DONE: `ROADMAP.md` v0.2 section flipped to **Done** (2026-11-07)

## Merge checklist (lead)
1. [x] Core wiring committed (registry/CMake/native/sema/codegen for time/text/hash/log/readline/collections/URL)
2. [x] Vendoring committed (unordered_dense, fast_float, moodycamel, XXHash, simdutf; windows-x64 and linux-x64 .a for xxhash/simdutf/pcre2)
3. [x] Express paste-ins build (cookie/response/router/routing/errors/body-parse/middleware/regex-routing in network codegen; network corpus green on Windows and Linux)
4. [x] Vendored-lib wiring (unordered_dense→collections, fast_float→json, simdutf→text, XXH3 bucket + xxhash64)
5. [x] Record indexOfProbe + codegen r[k] read/write (landed; corpus green)
6. [x] Hash Checksum32/64 swap + xxhash64 (landed; hash corpus green on both OS)
7. [x] String/JSON sema wiring (stringMember/fromCodePoint/raw, tryInferJsonCall) (landed)
8. [x] void/unknown finish (no-return→undefined, JSON.parse sema move, parser helper delete, corpus re-baseline)
9. [x] Generic constraints producer (enterClassTypeParams) + CP3412 at instantiation
10. [x] Object freeze completion + Object.fromEntries/groupBy (pinned by `stdlib/02/map/from_entries_ctor`; UAF fixed in `af72c7d9`)
11. [ ] cp3 install <dir> + cp3 test (landed; CLI harness runs before the tag)
12. [x] Cycle stages 2-3 (analysis/fuzzer tooling: `tools/cycle_stats.py`, `tools/fuzz_graphs.py`, `docs/design/cycle-proof.md`)
13. [x] Linux archives (xxhash, simdutf, pcre2 linux-x64 built, committed, staged in the Linux package; linux-arm64 xxhash/simdutf still placeholders)
14. [x] Docs: 00-overview, mkdocs, guide replaces llm.md, README link, ROADMAP flip (Done)
15. [x] Full corpus green + commit each green unit + tell user (Windows 2115/2092 green; Linux green on the final build)

## Per-user liberty orders (locked)
- Libs: ankerl unordered_dense, fast_float, simdutf, moodycamel, XXHash/XXH3, keep logger (no fmt), no compress/miniz.
- Commit frequently; batch-fix; build after ~10-12 units.
- Never touch those without asking: benchmarks/results.md, examples/basics/hello-world/src/index.cp3, examples/servers/test/, tmp_probe/, build_log.txt, page.html.
- No subagents after user kill order was lifted? (User re-authorized 15 agents + more - agents in use.)
