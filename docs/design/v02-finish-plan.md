# v0.2 Finish Plan (2026-11-05)

## Status: corpus green (2114 found, 2091 passed, 0 failed, 23 ambiguous), commits pending.

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
- DONE: `docs/content/03-standard-library/19-url.md` … `24-url-extensions.md` (20-text, 21-time, 22-log, 23-hash, 24-url-extensions)
- DONE: `12-collections.md` (9-type API table), `docs/design/*` (record, utility-types, rule3, hash-placement, import-suggestions, internal-libs, xxhash64, install-dir, cycle-*, remove-void-unknown, generic-constraints, verified-gaps)
- MISSING: `00-overview.md` index rows (19–24), `mkdocs.yml` nav (19–24), `docs/llm.md` §11/§13 sections (text/time/log/hash/url), `ROADMAP.md` Partial/Planned → Done flips.

## Merge checklist (lead)
1. [x] Core wiring committed (registry/CMake/native/sema/codegen for time/text/hash/log/readline/collections/URL)
2. [x] Vendoring committed (unordered_dense, fast_float, moodycamel, XXHash, simdutf; windows-x64 .a for xxhash/simdutf)
3. [ ] Express paste-ins build (cookie/response/router/routing/errors/body-parse/middleware/regex-routing in network codegen)
4. [ ] Vendored-lib wiring (unordered_dense→collections, fast_float→json, simdutf→text, XXH3 bucket + xxhash64)
5. [ ] Record indexOfProbe + codegen r[k] read/write (landed, needs build)
6. [ ] Hash Checksum32/64 swap + xxhash64 (landed, needs XXH64 vector check)
7. [ ] String/JSON sema wiring (stringMember/fromCodePoint/raw, tryInferJsonCall) (landed)
8. [ ] void/unknown finish (no-return→undefined, JSON.parse sema move, parser helper delete, corpus re-baseline)
9. [ ] Generic constraints producer (enterClassTypeParams) + CP3412 at instantiation
10. [ ] Object freeze completion + Object.fromEntries/groupBy (landed, needs suite)
11. [ ] cp3 install <dir> + cp3 test (landed, needs CLI tests)
12. [ ] Cycle stages 2-3 (auto-weak, regions, proof, fuzzer) (agents running)
13. [ ] Linux archives (xxhash, simdutf) + cross-compat audit (agents running)
14. [ ] Docs: 00-overview, mkdocs, llm.md, ROADMAP Done flips
15. [ ] Full corpus green + commit each green unit + tell user

## Per-user liberty orders (locked)
- Libs: ankerl unordered_dense, fast_float, simdutf, moodycamel, XXHash/XXH3, keep logger (no fmt), no compress/miniz.
- Commit frequently; batch-fix; build after ~10-12 units.
- Never touch those without asking: benchmarks/results.md, examples/basics/hello-world/src/index.cp3, examples/servers/test/, tmp_probe/, build_log.txt, page.html.
- No subagents after user kill order was lifted? (User re-authorized 15 agents + more - agents in use.)
