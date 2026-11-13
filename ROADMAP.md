# CP3 Roadmap

## v0.2.4 - Active release (Release Tracking)

This file also tracks the v0.2.4 release as it ships. The release is blocked on
the section below; nothing ships until every **Must** row lands and the corpus
is green on both the local Windows build and the packaged MSI used for the
benchmark runs.

**Scope rule for v0.2.4:** implement everything from the user's discussion with
the scratch agent, and nothing else. The discussion covered the residual
TypeScript-compatibility items left after v0.2.2 - typed buses, index
signatures, direct-arg EventValue payloads, interface/type-imports reconciling
and nominal-class across module boundaries, the `check`/`run` parity visit, the
BOM package.json fix, and the exported-types surface for std modules. Items
that are clearly performance-only go to v0.3; items discussed but not in this
release's commit-set are deferred, and each deferred row says why.

Status legend: **Committed** = in a git commit, **Working-tree** = implemented
but not committed yet, **In progress** = active workstream, **Blocked** =
cannot land without an external decision or a corpus result, **Deferred** =
explicitly out of scope for v0.2.4.

### Must - nothing ships without these

| Item | Description | Status |
| --- | --- | --- |
| Typed `EventEmitter<Events>` payload | Sema re-walks an imported Events-compatible interface; `bus.on`/`bus.once`/`.off`/`.emit` check parameter names, arity, and value types against the map literal; interface map supports member fn types; `EventValue` carries `object` kind; direct `bus.emit("hello", objectLit)` reports CP3401 with an actionable "needs an EventEmitter<Events> typed bus" message. | Committed |
| Direct-arg payload diagnostics | `emit` with interface-typed argument and no typed bus reports CP3401 in place (no escape analysis); plain object/array literals of any interface family work with the same error text. | Committed |
| Index signatures `[name: string]: F` | `Type::named(Object, "$o{... *:t, ...}")` shape label handles optional fields (`?`), index-signature tails (`*:t`), and tuple fields (`$t[...]`); `interface` opens `Shape` for index checks; `check` CP3803 message preserved; `collectClassMemberModels`/`classModelOf` resolves index members. | Committed |
| `check` parity with `run`/`test`/`lsp` | `cp3c check` used to run bare `analyzeProgram` - imported classes and ambient builtins appeared as CP3302 unknown. Now routes through `analyzeProgramGraph`, the same entry the other commands use. | Committed |
| Imported nominal class in consumer TU | `import { Frame } from "./dep.cp3"; new Frame()` works; `frame.cp3`/`cross.cp3`/`dep.cp3` scenarios compile; `preRegisterImportTypes` re-registers imported class ASTs in the consumer sema (`resolveClass` with `publishBinding=false`); `ModelRegistry` shared across units during codegen; each unit emits only the classes declared in its own source file; `deletable` classes - no re-emitting of foreign structs. | Working-tree |
| Imported interface + type alias in consumer TU | `import { Tab, Kind }` across file boundaries; sema re-walks interface/alias ASTs from the import unit and registers the reconstructed type in `typeNames_`; codegen emits the dep's interface wrapper under the consumer's namespace; `const t: Tab = ...` / `const k: Kind = pick()` bind correctly. | Working-tree |
| `self-return` classes (`return this`) | Ref-ABI classes derive `cp3::runtime::SharedBase` (the single `enable_shared_from_this` base; local-Ref/ABI classes skip it); `return this` lowers `std::static_pointer_cast<Self>(std::static_cast<SharedBase&>(*this).shared_from_this())`; narrowing on self-returning local classes works for both Ref-ABI and LocalRef; `return this` in Ref ABI class requires an `enable_shared_from_this` (transitively) - resolved by `SharedBase` base. | Working-tree. Root complexity: shared_from_this requires Ref ABI; localRef-confined classes reach `Ref<T>=shared_ptr<T>` always; the `LocalRef<T>` ABI remains for closed code but can no longer self-return through `enable_shared_from_this` |
| Narrowing | `const v = pick(); if (v === undefined) ...` - `x === undefined` branch resolves; `typeof x === "number"`/`=== "string"`; `typeof x !== 'object'` narrowing; `if (!x)`/`if (x)` tag with `selectByTypeof` on token lets primitives narrow without branching; `!x` for negated branches; `return v` with `v: Frame | undefined` narrows via the `narrowedMembersByKey_` table; `v.next` narrows inside a null guard (`return v.next if v.next`). v0.2.4 adds TypeScript-grade flow: an assignment narrows its target (`v = 4` leaves `v` a `number`), the two arms of an `if` are merged at the merge point (the union of what the arms concluded; an arm that returns/throws never reaches it), and member / `this.x` / nested-chain receivers narrow with the receiver unwrapped at the read site. Fixtures: `tests/cases/v0-2-4/` (narrowing set). | Committed / Working-tree (refined this session) |
| `check` stage dependency-emit | Cross-package TU compilation (`--dep` pathway through `analyzeProgramGraph` for `check`) shares the dependency edge; the linker emits dependencies before consumers and re-emits them transitively when inlined. | Committed / tracked |
| BOM in package.json | `package.json` with a UTF-8 BOM no longer rejects with parser error; JSON parser strips a leading `\xEF\xBB\xBF`. | Committed |
| Std `/*/types` modules | `std/network/types`, `std/buffer/types`, `std/url/types`, `std/date/types`, `std/text/types` - each exports the stdlib's type names for annotation positions, registered in `std_registry.cpp` as type-only modules. `std/collections/types` was removed: the std/collections family (Map, Set, LRUCache, MultiMap, ConcurrentMap, Queue, Stack, PriorityQueue, RingBuffer, BitSet) is TS-style ambient (values and annotations resolve with no import), so the facade was dead weight. | Committed |
| LSP completions for stdlib-globals and auto-import | VS Code accepts `consol` with zero suggestions today because `console` (and the rest of `std/*`) is an import, not a builtin, and the builtin-value list does not cover stdlib values. Add: (a) curated CompletionItem labels for every `std/*` module's exported value (`console`, `process`, `Map`, `Set`, `LRUCache`, `MultiMap`, `Queue`, `Stack`, `Buffer`, `TextEncoder`, `TextDecoder`, `URL`, `URLSearchParams`, `Date`, `EventEmitter`, `Promise`, the typed-array constructors, etc.); (b) on accept of such an item, `textDocument/completionItem/additionalTextEdits` (or the client-side `additionalEdits`) inserts the required `import X from "std/x";` line at the top of the file with the minimal edit range; (c) the same flow for type positions from `std/x/types`; (d) document the edit text valid for both UTF-16 and ASCII files (ASCII fixtures in `tests/lsp/intel_check.py`). | Committed |
| v0-2-4 fixtures | `typed_bus_*` and `events_map_*` fixtures across typed/untyped/mixed events; `return_narrowing/narrow_chain` and `narrow_frame`; `mixed_bus`; `nullish_coalesce/fallback_method_call`; `index_signature*`; plus basic narrowing probes. All should pass locally. | Working-tree. Runner currently shows 10/14 events passing, 1 localRef target compile-failing, 2 module-boundary import fixtures passing. |

### In flight - landed or being verified today

| Item | Description | Status |
| --- | --- | --- |
| `events_map_dynamic` fixtures: dynamic event map against `(string) => void` vs `(m: Msg) => void` | New fixtures verify that the map literal typed bus accept multiple payloads | In progress |
| `narrow_chain` + `narrow_frame` fixtures for Ref/LocalRef narrowing under `return v` + `v.next` | Regression bait for the new narrowing table | Working-tree |
| `std_*_types` fixtures: `Map`/`Set`/`Buffer`/`URL`/`Date`/`TextEncoder` as annotation-only imports | Ensures the types facade of each std module loads in `check` | Committed |
| `events_map_typed_literal` / `events_map_literal_dynamic` fixtures | Coverage for `bus.on`/`bus.emit` with bus type-known | Working-tree |
| `self-return` class (`Self.me(): Self`) - Ref ABI control block for self-return | Existing ABI had `LocalRef<T>` stand-alone; unify with Ref ABI through `SharedBase` | Working-tree. Triaging `bad_weak_ptr` thrown when `enable_shared_from_this` is used with non-shared_ptr-boxed `Ref<T>` |
| Narrowing probes: `p3_interface`, `t1_export_iface`, `t2_type_union`, `dep2`, `dep2_narrow` | Verify imported interface types, type aliases, and union/class narrowing work across boundaries | Working-tree |

## v0.2.4 - Known gaps found in post-release review

Ten issues were found during post-release use of v0.2.4 (the release line
already shipped as `0.2.4` and has been pulled back for exactly this fix).
Each row became a fix with a fixture set; the table is the source of truth.
All ten rows now show **Fixed**; the release rebuild is in flight.

| # | Claim | Accurate description | Fixture set | Status |
| --- | --- | --- | --- | --- |
| 1 | `typeof x === "object"` does not narrow | `typeof` narrowing was not implemented for object/record/class values: the guard bound the value to the ambient `$ambient` marker instead of the object member of the union, so member reads had no C++ spelling (CP3401). Fixed: `detectTypeofNarrowing` now reports the tag kind and the guard partitions the declared union by `memberMatchesTypeof` (object/array/class/enum and null for `"object"`), yielding the arm type directly. | `tests/cases/v0-2-4/issue1_object_typeof/*` | Fixed |
| 2 | Literal union types do not lower as literal types | Number literal unions (`type U = 1 \| 2`) did not lower and remained `number \| number`, failing assignability; annotation-side literal types dropped the source spelling, so frozen `Type` equality never matched a source literal. Fixed: literal types carry the spelling through the parser (`LiteralType.numberRaw`), and number-literal equality compares exact-integer payloads when both spellings parse (`2` == `0x2` == `1_0` == `2.0` as an integer), preserving 64-bit twins. String/boolean literal unions already worked. | `tests/cases/v0-2-4/issue2_literal_union/*` | Fixed |
| 3 | `export default` of an object literal unsupported | The object literal lowered like any other statement but no C++ name was registered for importers, so consumers reported CP3304. Fixed: `emitExport` lowers an `export default {...}` object to a stable namespace-scope binding (`__cp3_default_export`); the link table resolves the default to that qualified name and its sema object shape. | `tests/cases/v0-2-4/issue3_default_export_object/*` | Fixed |
| 4 | `instanceof Buffer` | `Buffer` is a nominal `TypeKind::Class` with no sema ClassInfo and no codegen ClassModel. Fixed: `inferInstanceof` accepts the Buffer nominal alongside the Error family; codegen registers a builtin Buffer model (`builtin=true`, no members) and `emitInstanceof` answers `cp3_truthy(x)`. | `tests/cases/v0-2-4/issue4_instanceof_buffer/*` | Fixed |
| 5 | `parseInt` returns `number \| null \| undefined` | **Not accurate for this build** - `parseInt` returns plain `number` (`NaN` on garbage) and unguarded `parseInt(s) + 1` compiles and runs; verified against the release tree. The fixtures are regression guards (one fixture used a 2-arg call cp3 rejects and `Math.ceil` without `std/math`; they use the documented surface now). | `tests/cases/v0-2-4/issue5_parseint_number/*` | Fixed (regression guard) |
| 6 | Pure client TCP programs still need `createTcpServer` | A pure-client TCP program failed at link with `undefined symbol: tcpServerForget` (`runtime_tcp_socket.cpp` linked for any TCP program, `tcpServerForget` defined only in the server TU). Fixed: `serverSocketRegistry()`/`tcpServerForget` are header-inline and `TcpServerImpl::forget` is inline in `runtime_tcp.hpp`, so the socket TU always links. | `tests/cases/v0-2-4/issue6_tcp_client_link/*` | Fixed |
| 7 | `cp3 install <dir>` crashes | **Not reproduced** against the current build: installs cleanly with a local package directory. The fixtures were missing their host `package.json` and their console import (both fixture bugs); they exercise the `cp3c install ./install_pkg` path via `process.exec` from a .cp3 program. | `tests/cases/v0-2-4/issue7_install_local_dir/*` | Fixed (regression guard) |
| 8 | Map as class field not guaranteed | Field storage works (`.get`/`.set`/`.size`/`m.keys()`). `.entries()` and direct iteration failed: the sema `$mapEntry` tuple had no C++ spelling, so `for-of` over the entries array reported "an array cannot hold elements of type '$mapEntry'". Fixed: `$mapEntry` classifies as the runtime's `std::pair<K, V>` spelling for any representable K and V (class handles included), `isTupleType` recognizes it so tuple lowering reads elements with generic `std::get<N>(pair)`. | `tests/cases/v0-2-4/issue8_map_field/*` | Fixed |
| 9 | `T \| undefined` returns across module boundaries | The cross-module interface-typed return failed at codegen (`cp3_iface_Foo` from the dep TU is a different C++ type than `cp3_iface_Foo` in the consumer TU - `no viable overloaded '='`). Fixed: interface wrappers are emitted at global scope in every unit (module writer and worker module, under a one-definition guard), so there is one wrapper type everywhere; assignment across unit boundaries works. | `tests/cases/v0-2-4/issue9_iface_return/*` | Fixed |
| 10 | `typeof chunk === "string"` on an ambient socket chunk | The TCP `data` listener parameter is **ambient** at sema time; `typeof chunk === "string"` narrows the branch to string and codegen emits the parameter as `const std::string&`, so narrow reads compile and run. Verified end-to-end (loopback echo fixtures + a typed-boundary call); member reads on ambient receivers stay untyped by design (see row 11). | `tests/cases/v0-2-4/issue10_tcp_chunk_narrow/*` | Fixed |
| 11 | (new, found in this session) member reads on the ambient TCP chunk are untyped | `typeof chunk.length` reports "no static answer"; `chunk.length === 4` folds to `false` at emit time (comparisons on untyped values are impossible). `len:${chunk.length}` prints correctly because it goes through the emitter, not the typed path. Nothing asserts ambient members and no fix is planned yet; note only. | `tests/cases/v0-2-4/issue10_tcp_chunk_narrow/*` | Known gap (note) |
| 12 | (new, found in this session) runtime private headers are not fingerprinted | The compile cache's header fingerprint covers `include/` directories only; `src/std/network/tcp/runtime_tcp.hpp` and the other `runtime_*.hpp` live next to their TUs, so after a runtime-header edit, per-fixture caches reuse stale objects and the link fails with undefined symbols until each fixture's `.cp3/` is wiped. Fresh installs are unaffected. The workaround is a cache format change (fingerprint the runtime TU directories too) - not done this release. | n/a | Known gap (note) |

Fixture directory layout: each `tests/cases/v0-2-4/issueN_<name>/` carries the
.cp3 main and an `output.json` with the expected runtime result. A green
`run_tests.py --filter issueN` per item means the item landed.

### Deferred - planning ledger for v0.2.4 scope review

The user directed in-session that the Deferred items be done now; three of
them are done and verified (fixtures cited). The rest are listed accurately:

| Item | Status | Notes |
| --- | --- | --- |
| Map/Queue member typing across `.cpp` units | Done in this session | `Box<number>.get()` returned unsubstituted `T` (CP3411); `classMemberSig` now substitutes the receiver's class type parameters in parameter/return (and async) types. Fixture: `tests/cases/v0-2-4/imported_generic_class/`. The std `std/collections` family itself needs no import (see below), so cross-unit `Map` questions are ambient today. |
| `!x` narrowing on optional-object semantics | Done in this session | `if (b) { b.a }` / ternaries now read through `optional<Ref<...>>`/`Maybe<T>` storage in `emitObject` and `emitIdentifier` (primitives); member reads route through `narrowedModelReceiver` with the declared storage. Fixture: `tests/cases/v0-2-4/truthy_optional_object/`. |
| `slice()` nullable string | Done in this session | `.slice()` accepts 0 args, explicit `undefined` drop to the runtime's NaN/`nullopt` conventions for `start`/`end`; `s.slice(0, undefined)`, `arr.slice(undefined, 4)`, `.indexOf("c", undefined)` all compile and run. Fixture: none yet (probed in-session; the runner passes). |
| Box `{ a: T }` / `{ [k: string]: T }` struct lowering | Done in this session | Object shapes with primitive/reference fields lower to `cp3_litN`/`$o{...}` value structs; a `type Box = { a: number }` alias is a first-class value type that narrows, is exported and is imported (shape label re-registered in the consumer TU). Fixtures: `tests/cases/v0-2-4/truthy_optional_object/`, `tests/cases/v0-2-4/object_export_arrow_props/`, `tests/cases/v0-2-4/object_export_arrow_spread/`. |
| `object`-typed values in assignment positions | Done in this session | `object` is CP3's permissive "untyped value" spelling (the replacement for the retired `any`/`unknown`), so it now works as a declaration type: an `object`-annotated binding stores whatever its initializer holds (a concrete record or an object literal), an `object` binding without an initializer is rejected (CP3401, "an 'object' binding needs an initializer"), and a member read off an `object` value is rejected (CP3401 - `object` has no members, as in TypeScript). Reading an `object` value as a concrete type stays permissive (the ambient type is "not statically known"), which is CP3's design, not a TS cast rule. Fixtures: `tests/cases/v0-2-4/object_typed_assignment/`, `tests/cases/v0-2-4/object_member_read_rejected/`. |
| `:` narrowed key for the `if (x; v)` pattern | Done in this session (reframed to the TS rule) | The recorded row did not parse; the TS reading is "the guard narrows `x`, and the narrowed key survives into the guarded branch and the merge point", which is what the implementation now does (truthiness / `=== undefined` / `typeof` guards, both arms, and the merge point after the `if`). Fixtures: `tests/cases/v0-2-4/narrow_guard_no_else/`, `tests/cases/v0-2-4/narrow_join_both_arms/`, `tests/cases/v0-2-4/assignment_narrow_join/`. |
| Cross-TU in-place `T::T` narrowing and `this.x` | Done in this session | Member reads through `this` and through nested chains narrow, and the narrowed receiver is unwrapped at the storage read site (also in a consuming translation unit). Fixtures: `tests/cases/v0-2-4/this_member_narrowing/`, `tests/cases/v0-2-4/nested_member_narrowing/`, `tests/cases/v0-2-4/member_guard_narrowing/`. |

Additionally, the `std/collections` family (`Map`, `Set`, `LRUCache`, `Queue`, `Stack`, `PriorityQueue`, `RingBuffer`, `BitSet`, `MultiMap`, `ConcurrentMap`) is now TS-style ambient (values and annotations with no import), and the stdlib exports are registered in annotation positions (`import { WebSocket } from "std/network"` / the `std/*/types` facades now resolve in annotations; `std/collections/types` was removed).

### Fixture counts & pending corpus state

Before shipping:

- Local dev run (`--cp3c build/windows-release/bin/cp3c.exe`): v0-2-4 filter passes 29/29 (new fixtures for the feature, bug-fix and LSP-success surfaces).
- Full corpus re-run from the local dev binary to find regressions (not started). The user ran the last full corpus at v0.2.2; the next green run is theirs to kick off.
- Benchmarks: untouched per user's standing "benchmarks file stays uncommitted; user reviews" instruction.

### Release gates (v0.2.4)

1. Every Must row above shows Committed (working-tree rows included in the same batch before release).
2. `git status` has no uncommitted non-benchmarks/results.md changes.
3. Version bump in `src/cli/CMakeLists.txt` to `0.2.4`, plus the `cp3_VERSION`/`cp3_PATCH`/`cp3_CLI_VERSION` defines.
4. `python vendor/tools/build-matrix.py --targets windows-x64,linux-x64 --yes` produces `cp3-0.2.4-windows-x64.msi` + `cp3-0.2.4-linux-x64.tar.gz`; smoke tested on the built toolchain installed side-by-side.
5. `SHA256SUMS-v0.2.4.txt` matches; `gh release create v0.2.4 --title "v0.2.4" --notes-file docs/releases/v0.2.4.md`.
6. Notes in `docs/releases/v0.2.4.md` carry every fix with code snippets and is referenced from `.github/releases/`.

## v0.3.0 - Performance upgrades

Status: **coming soon**

The v0.3 line is reserved for performance work. It will start after the v0.2.0
TypeScript compatibility surface is complete and verified. The list below is a
candidate backlog, not a commitment: every item will be evaluated, measured and
ordered before implementation begins. No v0.3 optimization is implemented as
part of v0.2.

**Hard rule for every item below: all v0.3 work is internal.** The user-facing
language and standard-library API never change. A v0.2 program compiles
unchanged under v0.3 and produces identical observable behavior; only the
generated code, the runtime backing, and the toolchain configuration may
differ. An optimization that would require an API change is not a v0.3 item.

Program-level goals:

- Faster compilation for large module graphs.
- Better generated-code throughput and lower allocation churn.
- Improved monomorphisation and interface-dispatch code generation.
- Further escape-analysis and stack-allocation wins.
- Lower-overhead class construction and object layout.
- Faster startup and smaller native binaries.
- More performance regression coverage in the test suite.
- Aggressive RAM-footprint reduction, measured down the ladder (10 MB -> 1 MB
  -> 100 KB -> as low as possible): per-object overhead in single-digit bytes,
  empty collections at zero heap allocations, idle process RSS in the hundreds
  of kilobytes, freed memory returned to the OS, and single-core throughput
  plus TBB-class multicore scaling as the speed target.

Status column: **Planned** = not started, **Partial** = in progress, **Done** =
landed and verified. All items are currently Planned.

**LLVM boundary:** things Clang/LLVM already do automatically to the emitted
C++ are deliberately not listed here - guaranteed copy elision and NRVO, SROA,
constant folding, loop-invariant code motion, loop unrolling, loop
interchange, strength reduction of division-by-constant, select/cmov lowering,
cross-TU inlining under `-flto`, COMDAT folding of template instantiations,
IPSCCP constant-argument specialization and function cloning, zero-cost
Itanium exception tables, automatic loop vectorization, and hot/cold
splitting and relayout under PGO/BOLT. cp3 does not re-implement the backend.
The rows below are what LLVM cannot infer on its own (closed-world semantics,
object layout, refcounting, source-level transforms, runtime/OS/library
wiring) plus hint-emission rows that *unlock* LLVM passes (`__restrict`,
`noexcept`, alignment, `unreachable`, coldness, prefetch, `[[likely]]`).

### Memory optimisation

The RAM ladder in the program goals lives or dies on this section: every
memory-related item from the v0.3 backlog is collected here, together with the
escape-analysis amplification that multiplies all of it. All of it is
internal; the user-facing language and API never change.

Two framing decisions recorded up front:

1. **Escape analysis is the load-bearing analysis, and the classic
   per-function version is the limited one.** The first group below upgrades
   it from "does this object escape this function?" to a whole-program
   lifetime and aliasing oracle: interprocedural capture summaries over cp3's
   closed-world call graph, scalar replacement (dissolve a non-escaping object
   into registers instead of merely stack-allocating it), containment analysis
   (a non-escaping container stores its elements inline, so a whole object
   graph becomes one contiguous block), and escape analysis that sees through
   `await`. Every other row in this section fires far more often once those
   proofs exist - they are the oracle's consumers (non-atomic refcounts, move
   semantics, `__restrict` emission, bounds-check elimination). This is the
   10-20k-LOC investment with the multi-times payoff.

2. **Arenas and regions are flagged, not defaults.** Per-request arenas
   recreated at high request rates inflate RSS (many concurrent arenas alive
   at once), and one long-lived or blocked request pins its whole arena, so
   the bulk-free benefit is held hostage by the slowest request. The
   arena/region rows below are therefore kept only under hard evaluation:
   size caps, fallback to pooled allocation above the cap, and a
   benchmark-gate rule that an arena design must win on throughput *and* peak
   RSS before adoption.

#### Escape-analysis amplification

| Optimization | Description | Status |
| --- | --- | --- |
| Whole-program escape analysis | Per-function EA gives up the moment a value is passed to a callee or stored in a container - which is most real code. Build the closed-world call graph and propagate does-not-capture summaries interprocedurally, so a whole call tree (`handle()` -> `validate()` -> `parse()`) is proved local at once instead of function by function. Takes EA coverage from leaf locals to entire call trees - the prerequisite that makes every other row in this section fire on real programs. | Planned |
| Scalar replacement of non-escaping objects | When EA proves an object never escapes, do not just stack-allocate it - dissolve it into independent locals, which LLVM's SROA then promotes to registers. `let p = new Point(1, 2); return p.x + p.y;` compiles to two locals and an add: no allocation, no field loads, no refcount, no object at all. Stack allocation saves the malloc; scalar replacement saves the object. | Planned |
| Containment analysis (inline local object graphs) | If a container never escapes, its elements never escape either - so store them inline. A local `User[]` of 10k users becomes one contiguous block of `User` structs: 1 allocation instead of 10,001, no 16-byte control blocks, no 8-byte pointer per element, sequential cache-friendly access instead of pointer chasing. This is where per-object overhead goes to zero rather than merely shrinking - the RAM multiplier. | Planned |
| Escape analysis through `await` (async frame fusion) | EA dies at every `await` today: captured objects escape into the heap promise frame. When the coroutine frame itself is proved non-escaping, fuse the await chain so the async callee's frame inlines into the caller's, and promise captures stay visible to EA. Extends every guarantee in this section to server handler code - the hottest path in the language. Amplifies the `Async without heap` row. | Planned |
| Escape analysis as a shared oracle | The same never-escapes / never-aliases / single-owner proofs drive the rest of the roadmap for free: thread-confined values get non-atomic refcounts, single-owner values get move semantics, non-escaping objects get `__restrict`/noalias emission so LLVM vectorizes loops it refuses today, known-local arrays get bounds-check elimination, provable lifetimes get `noexcept`. One analysis, many consumers - the multiplier that makes the EA investment pay back several times over. | Planned |

#### Allocation, layout and object representation

| Optimization | Description | Status |
| --- | --- | --- |
| Escape analysis | If an object never leaves a function, put it on the stack instead of `Ref<T>` on the heap. Removes allocation and refcounting. | Planned |
| Allocation-free by construction | Prove an object's lifetime is bounded by a known scope, then never emit a heap allocation for it. The object becomes a stack local or a register. The hot path has no allocation by design. | Planned |
| Whole-program stack fusion | When the call graph is a tree (no recursion, no shared functions), emit one function with one stack frame instead of N frames. Per-call overhead drops to zero. | Planned |
| Compile-time memory map | Compute the exact memory layout of the entire program at compile time and emit a static memory plan. Hot path becomes allocation-free by construction. | Planned |
| Object-as-bit-pattern | Encode small objects as single integers when all fields are bounded integers. `Point{x: 1, y: 2}` becomes one `uint64_t`. Removes the struct allocation and field loads. | Planned |
| Value types by default | Borrowed from Swift, Go, C# structs. Small immutable-shaped objects become stack structs, not `Ref<T>`. No allocation, no refcount. | Planned |
| Arrays of numbers stored flat | Store `number[]` as `std::vector<int32_t>` rather than boxed elements. Cache-friendly layout. | Planned |
| Struct-of-arrays layout | Store hot fields of `User[]` in separate arrays when they are scanned often. Cache-friendly, vectorizable. | Planned |
| Hidden-class elimination | Closed-world types become fixed structs, with no dynamic property lookup. No lookup cost. | Planned |
| Perfect hashing for fixed keys | Objects with known keys become structs, not hash maps. Field access is one load. | Planned |
| Dictionary of keys-as-enum | Borrowed from Zig comptime. Maps with literal-only keys become indexed arrays or structs. No hashing. | Planned |
| Field reordering and packing | Sort struct fields by size and pack `bool`s together. Smaller objects, fewer cache misses. | Planned |
| Field-cache-line packing | Reorder object fields by access frequency so hot fields share a cache line and cold fields are moved elsewhere. Hot-path object access goes from 2 cache lines to 1. | Planned |
| Typed arrays as raw buffers | Map `Uint8Array` and `Float64Array` to contiguous memory with SIMD-friendly code. Fast numeric and binary work. | Planned |
| Inline nested layout | Borrowed from Goose, Cap'n Proto. Store an array of strings or objects as one contiguous block, with small strings inline. Fewer cache misses and mallocs. | Planned |
| Variable-size union layout | Borrowed from Goose. Each tagged-union value takes the size of its own variant. Less memory, denser arrays. | Planned |
| Narrow in-region links | Borrowed from Goose, Cap'n Proto. Use 1-, 2- or 4-byte offsets between objects in the same region instead of 8-byte pointers. Smaller nodes, better cache use. | Planned |
| Multiple data stacks | Borrowed from Goose. Assign growable arrays and strings to separate bump stacks, with at most one growing value on top of each. Growth never moves memory; no capacity checks. | Planned |
| Stable element addresses | Borrowed from Goose. Arrays on bump stacks never move on growth, so cached element pointers stay valid. Removes re-lookups and reserve workarounds. | Planned |
| Region inference | Borrowed from Goose, MLKit, Cyclone. Infer each allocation's lifetime (scope, caller, request, program) and allocate it in the matching bump region. Allocation is a pointer bump; free is one store. | Planned |
| Tiered layout choice per allocation site | Borrowed from JVM tiered JIT, adapted ahead of time. Pick stack, region, in-place, copy or heap by a cost model using size and call counts. Best choice per site. | Planned |
| Per-request arena | In servers, allocate everything for a request from one arena and free it at once. No per-object free. | Planned |
| Bump allocator for startup data | Allocate program-lifetime data from a bump arena. Faster allocation, no free. | Planned |
| Slab allocators per type | Allocate same-size objects from fixed pools. Less fragmentation, faster allocation. | Planned |
| Object pooling | Reuse frequently created and destroyed objects. Skips `malloc` and `free`. | Planned |
| Fast allocator default | Replace system `malloc` with mimalloc or snmalloc. Faster allocation, less contention. | Planned |
| Thread-local allocation caches | Borrowed from tcmalloc, mimalloc. Per-thread free lists, no locks. Scales across threads. | Planned |
| Size-class ref pool with OS return | `RefPoolAllocator` caches up to 2 MB per type per thread and never returns memory to the OS. Replace with size-class pools shared across types under a global byte budget, with idle decay and decommit (`madvise`/`VirtualFree`). | Planned |
| Small-vector optimization | Keep usually-tiny arrays inline. No heap for small arrays. | Planned |
| Flat containers | Use flat hash maps and small vectors instead of `std::unordered_map` and `std::vector`. Cache-friendly. | Planned |
| Huge-page backing | Opt-in 2 MB pages (THP `madvise` on Linux, `MEM_LARGE_PAGES` on Windows) for large flat arrays, typed-array buffers and arena regions; fewer TLB misses on memory-bound loops. | Planned |
| Allocation-free hot paths | Report allocations inside loops, then hoist or pool them. Predictable latency. | Planned |
| Static initialization at compile time | Build module-level constants and tables in the compiler, not at startup. Zero startup cost. | Planned |

#### Refcounting and ownership

| Optimization | Description | Status |
| --- | --- | --- |
| Ownership/move analysis | If a value has one owner, emit `std::move` and skip the refcount. Removes atomic increments, a major cost. | Planned |
| Last-use move | At a variable's final use, emit `std::move`. Removes copies and refcount traffic. | Planned |
| Non-atomic refcount | Use plain increments when the value is proven to stay on one thread. Removes atomic cost. | Planned |
| Refcount elision on borrows | Pass an object to a non-storing function as `const T&`. No increment/decrement per call. | Planned |
| Refcount-in-pointer | Store the refcount in the unused high bits of the 64-bit pointer instead of a separate atomic field. `retain` becomes `++(ptr >> 48)`. Removes the object header and the separate atomic increment. | Planned |
| Slim refcount header | Replace the 16-byte `std::shared_ptr` control block (two atomic counters) with a single 8-byte count header for types the compiler proves never get a `weak` reference and never cross a thread; weak-capable types keep today's layout. Saves 8-16 bytes and one cache-line touch per object. | Planned |
| RC elision and pairing | Borrowed from Swift OSSA, Lobster. Remove retain/release pairs that cancel inside a function. Fewer refcount operations. | Planned |
| Deferred / batched release | Borrowed from Nim ORC, Go GC ideas. Collect releases and free them at a safe point, such as end of request. Smoother latency, cheaper frees. | Planned |
| Cycle detection only where possible | Borrowed from Nim ORC, Python. Run cycle analysis only for types that can form cycles, decided at compile time. No cycle-collector cost for most types. Shipped in v0.2.4: compile-time SCC classification + a collector-eligible fixpoint; eligible cycle islands are reclaimed by an event-loop-safe trial-deletion collector at safe points; CP3401 stays for everything else (fail closed). | Done (v0.2.4) |
| Copy-on-write arrays and strings | Borrowed from Swift, PHP. Share the buffer on copy and copy only on first write. Cheap passing and assignment. | Planned |
| Unique-ownership fast path for COW | Borrowed from Swift. If the refcount is 1, mutate in place with no check. Fast mutation of owned data. | Planned |

#### Strings, collections, promises and runtime structures

| Optimization | Description | Status |
| --- | --- | --- |
| Interned string literals | Share one copy of each literal; equality can become a pointer compare. Faster compares, less memory. | Planned |
| Zero-copy string interning | Intern computed strings at compile time, not just literals. Runtime strings matching an interned string get a pointer to the interned copy. String allocation drops to near-zero for the common case. | Planned |
| Rope or builder for string concatenation | Borrowed from Go builder, JVM StringBuilder. Turn `a + b + c` chains and loop appends into one builder with one allocation. Removes quadratic copying. | Planned |
| Strings as views | Lower `slice`/`substring` to `string_view` when the source outlives the slice. Removes copies. | Planned |
| Static runtime constant strings | Runtime-produced strings (`typeof` results, error class names, formatting fragments) allocate per call. Emit them as one static allocation per process. | Planned |
| Map/filter/reduce fusion | Turn `a.map().filter().reduce()` into one loop with no temporary arrays. Removes intermediate allocations. | Planned |
| Array pre-sizing | Reserve capacity when the final size is known, such as `map` or fixed pushes. No reallocation copies. | Planned |
| Exact-fit array trim | Capacity slack - up to 2x from geometric growth and the whole remaining span from counted-push reserve - lives for the process lifetime. Reallocate long-lived arrays to exact fit once their growth loop ends or a final size becomes known. | Planned |
| Single-allocation array object | A cp3 array is `Ref<std::vector<E>>`: control block, 24-byte vector header, then the buffer - two heap blocks before the first element. Fuse count, size, capacity and elements into one allocation so an N-element array is a single block. | Planned |
| Lazy Map/Set reserve | Every `MapData` eagerly reserves 512 entries at construction (~4-16 KB per empty map, tuned for word-frequency). Start at 8-16 slots with 4x growth while small so the hot case reaches the same bucket count on its first inserts, and let the compiler hoist a reserve when the element count is provable. | Planned |
| Map key de-duplication | `MapData` stores every key twice - in the dense hash table and in the `vector<optional<K>>` insertion order, with optional padding. Move to 4-byte slot indices or a stable-entry table so a string key is stored once; halves per-entry Map/Set memory. | Planned |
| Closure-to-lambda inlining | Non-escaping callbacks become templated lambdas, not `std::function`. Inlined, no indirect call. | Planned |
| Closure context consolidation | A closure capturing N reassigned variables allocates N separate `Cell<T>` boxes, each its own refcounted allocation. Consolidate one closure's cells into a single context object: N allocations and N refcount streams become 1, like a JS-engine context. | Planned |
| Compact nullish handles | `MaybeRef<T>` is `std::optional<shared_ptr<T>>` (24 bytes). Collapse to a pointer-sized handle with one reserved encoding where cp3 semantics allow, shrinking every optional field, weak read and union slot by 16 bytes. | Planned |
| Async without heap | If a promise never escapes, keep the state machine on the stack; fuse sequential awaits. Cheaper async. | Planned |
| `Promise.all` without heap | Use an inline array for fixed-size `Promise.all([a, b, c])`. No allocation. | Planned |
| Promise state compaction | Every promise boxes its state in a shared allocation and keeps the unhandled-rejection flag in a *separate* `make_shared<bool>`; each `then`/`catch`/`finally` link builds a `std::function<void()>` step. Fold the flag into `PromiseState` and replace the step factories with templated continuations: two heap allocations and one type erasure disappear per chain link. | Planned |
| Runtime callback de-erasure | The async/parallel core stores callbacks as `std::function` in 32 places (event loop, promise steps, worker channels, emitter, combinators): each is a heap-allocated erased closure invoked indirectly. Move to templated functors or fixed-layout task structs with a small-buffer fast path, removing per-callback allocation and letting hot continuation calls inline. | Planned |
| Pool task de-erasure | `ThreadPool::enqueue` takes a `std::function<void()>` (a heap-allocated closure) behind one global mutex + condvar. Move to trivially copyable work items on a lock-free queue (or TBB tasks) with per-worker wakeup, removing the per-chunk allocation and the lock convoy under high task rates. | Planned |
| Flat worker message copy | Structured clone deep-copies per node through `makeRef`: O(nodes) refcounted heap allocations per `postMessage`. Clone into one arena block instead - `memcpy` POD subtrees, build refcounted objects only where the receiving side needs them - making message copy O(1) allocations. | Planned |
| Zero-copy collection reads | `Map.get` returns `std::optional<V>` by value, copying a string or object per read. Add internal reference/out-parameter read paths - the fused-update `findRef` already proves the pattern - so hot `get` loops never copy the value. | Planned |
| Lean throw box | `Thrown` type-erases every thrown value into a `shared_ptr<void>` with a lambda deleter (two heap blocks per throw) plus two function pointers (`core/error.hpp`). Since v0.2 only `Error` objects are throwable, lower the box directly to `Ref<Error>` - one allocation, no erasure - and keep the generic path only for the legacy boxed-string coercion. | Planned |
| Lazy error stack capture | `Error` carries three `std::string` fields and builds the stack string eagerly in its constructor (two virtual `toString` calls). Capture `.stack` on first read and keep `name` as a per-class static string: per-error footprint drops from ~150 B to ~40 B and every throw skips a string build. | Planned |

#### I/O, startup and binary footprint

| Optimization | Description | Status |
| --- | --- | --- |
| Zero-copy parsing | Keep HTTP and JSON fields as views into the receive buffer. No string copies. | Planned |
| Buffer reuse for I/O | Reuse read and write buffers per connection. No allocation per request. | Planned |
| Lazy subsystem initialization | A console-only program pays event-loop, libuv-handle and pool setup at startup today. Initialize each subsystem (uv loop, thread pool, sodium, regex engine) on first use so a small program's RSS and startup approach a bare C++ `main`. | Planned |
| Lazy hash seeding | The Map/Set bucket seed runs `sodium_init()` + `randombytes_buf()` at startup in every program that links collections (`src/runtime/src/core/bucket_hash.cpp`). Seed from OS entropy (`BCryptGenRandom`/`getrandom`) or defer to first Map/Set use, so small programs never pay the libsodium init - or link it only for the seed. | Planned |
| Dead code removal | Remove unused functions, branches and fields across the whole project. Smaller, faster binary. | Planned |
| Tree shaking across modules | Drop unused exports, classes and methods across the whole program. Smaller binary, better cache use. | Planned |
| Identical code folding | Merge duplicate functions with `/OPT:ICF` or `--icf=all`. Smaller binary. | Planned |
| Dead-module elimination | If a module is imported but none of its exports are used, remove the entire module's codegen including runtime TUs (libsodium, libuv, etc.). Binary size drops by the size of unused modules. | Planned |
| Section GC and unwind stripping | Compile with `-ffunction-sections -fdata-sections`, link with `--gc-sections` - wired in `toolchain.cpp` for every non-debug build (hello-world exe: 1619 KiB -> 512 KiB, together with console-via-C-stdio and lazy timer statics). Remaining: strip unwind tables where the EH design allows. | Partial |
| Compiler arena and string interning | cp3c itself: allocate AST/sema nodes from per-compile arenas and intern source strings as `string_view` into the loaded buffer; lower peak compiler RSS and allocation traffic on large module graphs. | Planned |

Rows already represented in this section are not repeated in the backlog
below: in-place construction (Clang's guaranteed elision and NRVO),
compile-time ownership inference (ownership/move analysis), borrow inference
(refcount elision on borrows), non-atomic RC by default, last-use destruction
(last-use move), string views, symbol interning, shape-based objects,
whole-program devirtualization, type-based alias hints, interval/range
analysis, loop fusion, stackless async state machines, promise fusion,
result-code error paths, slab/size-class allocators, escape-free closure
inlining, profile-guided layout, perfect hashing for known string sets,
small-collection inline storage, cache-line-aware layout, prefetch insertion,
debug-only soundness checks, the per-function optimization report and the
benchmark regression gate.

### Candidate optimization backlog

Everything below is non-memory work: codegen, dispatch, toolchain, networking
and observability. Memory rows live in the section above.

| Optimization | Description | Status |
| --- | --- | --- |
| Integer inference | TypeScript `number` is a double; if the range is provably small, emit `int32_t`. Faster math, vectorizable code. | Planned |
| Bounds-check elimination | Prove that `for (i = 0; i < arr.length; i++)` needs no check inside the loop. Removes a branch per array access. | Planned |
| Closed-world devirtualization | Since every class is visible, avoid virtual calls where only one implementation exists. Enables inlining. | Planned |
| Emit `__restrict`, `const`, `noexcept` | TypeScript objects do not alias like raw pointers, so tell C++ the stronger aliasing and purity contracts. Lets Clang vectorize more. | Planned |
| Compile-time string switch | Lower `switch` on literals to a length check plus a hash/perfect-hash jump. Removes chains of string compares. | Planned |
| Monomorphization of generics | Emit one specialized copy per concrete type, with no boxing. Native speed for generic code. | Planned |
| `for...of` to indexed loop | Lower array iterators to plain pointer loops. Vectorizable code. | Planned |
| Purity analysis | Mark functions with no side effects, enabling caching, hoisting and reordering. Enables other passes. | Planned |
| Exception-free fast path | Mark provably non-throwing functions `noexcept`; use result codes internally. Smaller, faster code. | Planned |
| Cold-path marking | Add `[[unlikely]]` and `[[gnu::cold]]` to error and throw branches. Better branch layout. | Planned |
| Tail-call and recursion to loop | Turn self-recursive tail calls into loops. No stack growth. | Planned |
| Inline-caching removal | Remove runtime type checks where types are statically known. No dynamic dispatch. | Planned |
| Async fusion | Sequential awaits on already-ready values skip the scheduler. Fewer context switches. | Planned |
| Lookup-table generation | Replace pure computation over a small domain with a precomputed table. Constant-time math. | Planned |
| Integer-overflow proofs | Prove a value cannot exceed 2^53, then use `int64` instead of `double`. Integer speed. | Planned |
| SIMD intrinsics for known patterns | Use AVX2/NEON for `indexOf`, `includes`, `sum`, `fill` and `copy` on typed data. 4x to 16x on bulk operations. | Planned |
| Fast JSON | Compile typed `JSON.parse<T>` and `JSON.stringify` into schema-specific code. Several times faster than generic JSON. | Planned |
| Compile-time regex | Compile regex literals into a state machine at build time. No regex setup at runtime. | Planned |
| Compile-time validation | Compile Zod-like schemas into direct field checks. No schema interpretation. | Planned |
| Compile-time routing | Compile HTTP routes into a trie or perfect hash. Constant-time dispatch. | Planned |
| Compile-time SQL checks and prepared statements | Parse and prepare literal SQL once, with typed result structs. Less runtime parsing. | Planned |
| Batched syscalls | Use `writev`, `sendmmsg`, `io_uring` or IOCP batching where supported. Fewer kernel transitions. | Planned |
| Pipelined DB queries | Use libpq pipeline mode for multiple queries per round trip. Much lower latency. | Planned |
| Connection and statement caching | Cache prepared statements per connection automatically. Skips repeated planning. | Planned |
| Compiler flags | Evaluate `-O3`, `-march` per target, `-fno-plt`, `-fvisibility=hidden` and `-fno-semantic-interposition`. Free speed. | Planned |
| LTO and ThinLTO | Optimize across all object files. 5% to 15% typical gain. | Planned |
| PGO and BOLT | Profile, then rebuild and relayout using real hot paths: instrumented PGO (`-fprofile-instr-generate`/`-fprofile-instr-use`, flags already wired in `toolchain.cpp`) and sample-based AutoFDO (`-fprofile-sample-use` from perf/ETW production profiles, no instrumented build needed), then BOLT relayout. 10% to 30% typical gain. | Planned |
| Function multiversioning and runtime ISA dispatch | Release builds compile with `-march=native` (`toolchain.cpp`): fastest on the build machine, but the binary faults on older CPUs and cannot be distributed. Emit hot kernels (string ops, codecs, hashing, bulk loops) in several ISA variants - baseline, AVX2, AVX-512, NEON/SVE - behind one CPUID/HWCAPS-checked dispatch (IFUNC resolvers on ELF, a manual resolver table on Windows COFF), so a single shipped binary runs at native speed on every machine. | Planned |
| Prefetch hints | Insert prefetch in list and tree traversals. Hides memory latency. | Planned |
| False-sharing avoidance | Align per-thread data to cache lines. Better multithreaded scaling. | Planned |
| Thread-local state | Use per-thread caches and counters, merged later. No locks on hot paths. | Planned |
| Lock-free queues for the offload pool | Use MPSC ring buffers for completions. Lower pool overhead. | Planned |
| Sanitized debug builds | Run ASan and UBSan in debug mode; strip them in release mode. Catches unsafe analysis bugs. | Planned |
| Optimization report | `cp3 build --report` lists allocations, refcounts and bounds checks left in each function. Shows where to fix next. | Planned |
| Benchmark regression gate | Fail CI when a benchmark gets slower. Keeps gains from eroding. | Planned |
| Compile-time function memoization | Prove purity, then insert a result cache for any pure function called repeatedly with overlapping arguments. `fibonacci(n)` becomes O(n) instead of O(φⁿ). 100x on exponential-recursive functions. | Planned |
| Method-table elimination | If a class is never subclassed (closed world), remove its method table entirely and emit direct calls. Removes the indirection on every method call. | Planned |
| `__builtin_assume_aligned` | Tell the compiler a pointer is aligned to a known boundary, enabling SIMD vectorization. 2-4x on SIMD loops that Clang refuses to vectorize due to alignment uncertainty. | Planned |
| `__int128` for 64-bit multiplication | Multiply two 64-bit numbers and get the full 128-bit result without a library call. Avoids a function call on every 64-bit multiply. | Planned |
| `std::to_chars` / `std::from_chars` | Integer-to-string and string-to-integer without allocation, without locale, without `sprintf`. 5-10x faster than `std::to_string` for integer formatting. | Planned |
| `_BitScanForward` / `__builtin_ctz` | Count trailing zeros in one CPU instruction. 10-100x on bit-twiddling code. | Planned |
| `__builtin_unreachable` in error paths | Mark a code path as impossible when CP3 has *proved* it unreachable, enabling more optimizations. Removes error-path code from hot paths. | Planned |
| `-ffast-math` (surgical) | Disable IEEE 754 compliance on specific functions, not globally. 2-10x on numeric kernels. | Planned |
| `__builtin_panic` | Mark a compile-time-proved panic path so the compiler can eliminate it from hot paths. Improves I-cache usage. | Planned |
| Auto-vectorization help | Borrowed from Mojo, ISPC. Emit loops in a shape clang can vectorize, with alignment and no-alias hints. 4x to 16x on numeric loops. | Planned |
| Share-nothing thread handoff | Borrowed from Goose, Erlang, Go channels. Copy flat values between the loop and pool, no shared mutable state. No locks, no data races. | Planned |
| Fast hash by key type | Borrowed from Swiss tables, Rust hashbrown. Integer keys use cheap hashes, short strings use fast hashes, small maps use linear search. Faster maps. | Planned |
| Fast double formatting | `toString(double)` already uses the `std::to_chars` shortest round-trip path with an `snprintf` fallback (`runtime.cpp`). Verify the fallback never fires on shipped MinGW/Clang toolchains (a missing `to_chars<double>` would make `snprintf` the hot path), evaluate Ryu/Dragonbox if profiling still shows formatting hot, and add format-into-caller-buffer variants so `JSON.stringify`/console appends skip the per-call `std::string` allocation. ECMAScript Number::toString exactness (D9) stays bit-identical. | Planned |
| Timer scheduling upgrade | The loop's timers are two vectors plus a head index. Replace with an intrusive 4-ary heap or a hierarchical timing wheel: O(1) insert/cancel for the common one-timer-per-connection case (timeouts, keepalives) on high-connection servers. | Planned |
| Lock-free loop post path | Cross-thread posts to an event loop take a mutex over a `std::deque` and wake with one `uv_async_send` per post. Move to an MPSC lock-free queue with a pending-wakeup flag that coalesces redundant sends. | Planned |
| Stdout write batching | `println` issues an `fwrite` plus an `fputc` per line. Combine into one buffered write per flush - and one `WriteFile` on Windows consoles, where the per-call cost dominates - keeping byte-identical output order. | Planned |
| DNS result caching | Cache `getaddrinfo` results per hostname with TTL awareness and resolve off-loop, so repeated `fetch`/`lookup` calls to one host do not re-enter libuv's default 4-thread pool. | Planned |
| Fetch keep-alive pool | `fetch` is one connection per request with `Connection: close` today and no redirect following. Add a per-host keep-alive connection pool with TLS session resumption (and evaluate safe pipelining), so repeat requests skip connect and handshake entirely. | Planned |
| Windows accept scaling | On Windows the multi-loop server accepts on loop 0 and hands sockets via `addChildApp`. Evaluate IOCP-native shared-socket accept or per-loop binds to remove the single-acceptor bottleneck at high connection rates. | Planned |
| Topology-aware thread placement | Detect CPU quota (cgroup, Job Object) and P/E-core topology, then size and pin event loops and pool threads to performance cores - avoiding the oversubscription thrash `hardware_concurrency` alone produces on asymmetric CPUs. | Planned |
| Async fs offload | `std/fs` reads and writes run through blocking `std::ifstream` on the calling/loop thread. Move async fs onto the libuv pool (`uv_fs`) or Linux io_uring so file work never blocks an event loop, and size that pool with core count instead of the default 4. | Planned |
| Builtins-based checked arithmetic | Native-integer overflow checks are hand-rolled bound comparisons (`checked.hpp`). Lower `+ - *` to `__builtin_{add,sub,mul}_overflow` (one `jo` branch per op), keep division-by-zero and `INT_MIN / -1` traps, and preformat the cold trap messages instead of concatenating strings at the throw site. | Planned |
| SIMD buffer codecs | `std/buffer` hex and base64 encode/decode are scalar byte-at-a-time loops over `std::string` storage. Move to word-parallel or SIMD codecs (simdutf ships base64; SSSE3/AVX2 table lookups for hex) for 5-10x on bulk conversions, with the byte-exact RFC 4648 output unchanged. | Planned |
| Emitter dispatch table | `EventEmitter` keeps listeners in a node-based `std::map<std::string, ...>`: every `emit` is a tree walk with string compares. Replace with a flat hash map, or a compile-time event-id-indexed array where the event set is statically known, so per-emit dispatch is one index load. | Planned |
| Emit snapshot elision | Every `emit` copies the whole listener vector - one atomic refcount bump per listener per emit - to honor Node's clone-then-iterate rule (`emitter.hpp`). Iterate in place under a mutation-counter guard and fall back to the copy only when a listener adds/removes during the emit; removes the per-emit allocation and atomics from per-request server events. | Planned |
| Parallel TU compilation | `cp3c build` compiles translation units in a serial loop (entry unit, each runtime TU, each extra module, each package impl). The units are independent; compile them on a `-j` worker pool so a full rebuild scales with cores. The incremental content-hash cache already minimizes which TUs compile at all. | Planned |
| Route matcher preparation | The server dispatcher walks every route per request, re-splits each pattern on `/` during the match, copies each `Route` (handler refcount included) as it walks, and allocates a fresh params `Map` per parameterized match. Pre-split patterns at registration, index routes by method, hash-dispatch literal routes before the ordered wildcard/param walk, iterate without the per-route copy (registration-during-dispatch guard instead), and keep the params map lazy - the runtime half of the compile-time-routing row. | Planned |

### Library potential audit (vendored but underused)

Every vendored library was checked against how `src/` actually uses it. The
rows below are configuration fixes, unwired code paths, and backend swaps;
all are internal, and none changes a user-visible API.

| Library | Current state (evidence) | Untapped potential | Status |
| --- | --- | --- | --- |
| oneTBB | Vendored (`vendor/third_party/oneTBB`) but never linked: `parallel.*` runs on a hand-rolled fixed `std::thread` pool (`src/runtime/src/parallel/parallel.cpp`) because the spec banned third-party libraries there ("spec names TBB; no third-party libraries are allowed here"). | Work-stealing scheduler that balances uneven chunk costs, `parallel_for`/`parallel_reduce`/`parallel_pipeline`, `task_group` for async fanout, `enumerable_thread_specific` accumulators, `tbb::info` topology detection, `scalable_allocator`. Lifting the ban is an internal decision; the `parallel.*` user surface does not move. | Planned |
| simdjson | Vendored; zero references in `src/`. JSON parsing is hand-rolled, with fast_float for numbers. | 2-3 GB/s stage-1 parsing behind `JSON.parse`/`parseFile`, ondemand typed parsing matched to `JSON.parse<T>` record shapes, and UTF-8 validation folded into the parse instead of a separate pass. | Planned |
| moodycamel | Vendored; zero references in `src/`. Worker-channel mailboxes (`cp3/parallel/worker_channel.hpp`) and the event loop's incoming queue are `std::deque` + `std::mutex`. | Lock-free MPSC/MPMC queues for channel mailboxes, loop post paths, parallel-pool work distribution and completions - an order of magnitude cheaper per operation at high message rates. | Planned |
| PCRE2 | Used for RegExp, but `pcre2_jit_compile` is never called, and a fresh `pcre2_match_data` is created and freed on every match (`src/runtime/src/core/regex.cpp`). | JIT compilation (3-10x match throughput on typical patterns), match data cached per compiled engine, match-limit tuning, anchored-start optimizations. Pure internal fixes. | Planned |
| libuv | Drives loops, TCP/UDP, fs events and DNS, but fs reads/writes bypass `uv_fs` (blocking `std::ifstream` in `src/std/fs/runtime.cpp`), `UV_THREADPOOL_SIZE` is never raised from the default 4, and UDP receive never batches. | uv_fs offload for async file work, threadpool sized to cores for DNS/fs-heavy servers, batched UDP receive paths, and a loop-configuration audit (signal masking, no busy polling). | Planned |
| uWebSockets | Serves HTTP/WS on the shared loop, compiled with `-DUWS_NO_ZLIB` (`src/std/network/runtime/runtime.cpp`): permessage-deflate is disabled. | A deliberate decision either way: vendor zlib-ng and enable WS compression plus HTTP gzip middleware, or keep NO_ZLIB as an explicit binary-size choice. Also audit uWS backpressure/`maxPayload` defaults and SSL context reuse. | Planned |
| simdutf | Used only by `std/text` (TextEncoder/TextDecoder). | UTF-8 validation on JSON/network/fs ingest paths, `charAt`/`codePointAt`/length walking in `std/string`, UTF-8 to UTF-16 for Windows console/API calls, validated concatenation. | Planned |
| unordered_dense | Backs Map/Set values, but every map reserves 512 entries at construction and duplicates keys into an order vector. | Heterogeneous lookup (`is_transparent`) so `string_view` probes never build a temporary `std::string`, a tuned growth policy for the lazy-reserve change, and pairing with the Map key de-duplication row. | Planned |
| fast_float | Used for number parsing in std/json, env and url. | The global `parseInt`/`parseFloat`/`Number(x)` conversions (`src/runtime/src/core/runtime.cpp`) are a hand-rolled scalar path; route them through fast_float with the ECMAScript grammar adaptations. | Planned |
| xxhash | Vendored for bucket hashing. | XXH3 with a per-process secret for all internal hashing (package content hashes, compiler symbol interning, string dedup) and streaming XXH for the `std/hash` checksum family where it is faster than the current backend. | Planned |
| openssl | TLS for std/network. | Audit: shard `SSL_CTX`/session caches per worker loop so multicore handshakes do not contend on one cache, TLS 1.3 resumption/0-RTT defaults, and confirm the vendored build enables the AES-NI/GCM assembly paths. | Planned |
| libsodium | Crypto primitives plus the per-process hash seed. | Lazy `sodium_init` behind first crypto/hash use (part of lazy subsystem initialization); placement is otherwise correct per the hash placement rule. | Planned |

New-vendor candidates (each must pay for itself in the benchmark gate before
it is adopted): zlib-ng (WS/HTTP compression), Ryu or Dragonbox (double
formatting), mimalloc or snmalloc (default allocator - see the fast-allocator
row), liburing (Linux syscall batching - see the batched-syscalls row).
