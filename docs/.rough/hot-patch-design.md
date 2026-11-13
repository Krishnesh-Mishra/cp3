# Native hot patch - dev-only design (Phase 4)

**Status:** design proposal. Binding for the v0.2.5 incremental/dev fast path once
the lead rules on D82/D83 (section 16). Nothing here ships in release. This
document is the contract that the implementation must not contradict.

**Scope:** DEV builds only (`BuildProfile::Incremental`, i.e. `cp3c run` /
`cp3c dev` in the dev profile). Release stays exactly as it is: direct calls,
static state, static runtime, one static binary (D56). No new user flags, no
second runtime, no interpreter, no JIT, no release behavior change. EA stays
on; D9 number text and D62 microtask/timer order are preserved exactly; dev and
release produce byte-identical output for the same source.

**Goal:** a body-only edit (interface + state layout unchanged) applies to the
RUNNING process in ~100-300 ms without relink and without restart; an
interface/layout edit falls back to rebuild + relink + restart with the old
program kept alive until the new one is ready; an error or an ABI mismatch
refuses the swap, keeps the old program, and reports diagnostics.

---

## 1. What exists today (the machinery this design reuses)

Everything below is reused as-is or extended additively; nothing frozen is
reshaped (D14).

| Existing fact | Where | How the design reuses it |
| --- | --- | --- |
| Whole program lowers to ONE TU (release) or plural pipeline (Incremental): one shared header `cp3_program.hpp` + one `cp3_unit_N.cpp` per checked unit + one entry TU | `src/codegen/src/emit/decl/program.cpp` (`sharedProgramHeader` :300, `unitModuleSource` :336, `entryModuleSource` :348, `assembleProgramModules` :361; `cp3_unit_N` naming at :370) | The per-unit TU is exactly the module lib's single translation unit. The plural pipeline is what Incremental already uses (`run.cpp` :172-198). |
| Per-unit C++ namespace `cp3_mod_<name>`, phase-1 declarations then phase-2 definitions; non-entry units get `cp3_module_init()` | `program.cpp` `appendUnit` :86, `unitInitDef` :71, phase split :277-283 | Module libs export the initializer through the descriptor; namespaces are untouched. |
| Cross-module calls resolve through the link table to qualified names (`cp3_mod_<unit>::<name>`); live-binding semantics | `detail/generator/program_link.h` (`ProgramLink` :61, `UnitSurface` :53, `LinkedName` :39) | The descriptor's table order is derived from this table, canonically sorted (section 4). In dev, call sites to foreign units lower to table calls instead of direct qualified calls (section 6). |
| Incremental manifest keys unit TUs on the fnv1a64 of the SHARED header plus the include-tree fingerprint; a body-only edit leaves the shared header byte-identical, so unchanged unit TUs stay cached | `build.cpp` :91-94 (comment), :547-557 (`unitFingerprint`), :572 (units), :581 (entry) | The same content hash decides "body-only candidate" vs "interface candidate", before any compile happens. |
| PCH for the dev profile (`_cp3_pch.pch`), reused when compiler key + profile + header fingerprint + prefix content match | `build.cpp` :434-531 (`kPchPrefixSource` :447, `buildOrReusePch` :484) | Module lib compiles reuse it. ~100-200 ms per TU at -O0 expected (the PCH comment at :438 reports ~280 ms for a cold runtime TU with it). |
| `.cp3/build/` holds intermediates only; exe in `.cp3/out/` (D61); manifest v3 with header fingerprints and link stamps | `cache.cpp` `makeBuildLayout` :33, `saveManifest` :100; `build.cpp` link-stamp helpers :717-760 | The dev artifact tree `.cp3/dev/` mirrors this but is separate; `clean` removes it like `.cp3/build/` (noted in section 13). |
| Per-module export surface as `ImportedBindings` (`functionBinding`, `valueBinding`, `defaultExport`) inside the CLI's `ExportSurfaceMap` | `src/cli/src/toolchain/runtime/pipeline.cpp` :37-46, :83-101, :147-160, :296-360; consumed by `analyzeProgramGraph` | The interface hash for one unit is computed from exactly this structure, after sema, in the same process (section 7.3). |
| Interface hashing exists and is not yet wired: `serializeInterfaceSignature`, `interfaceHashOf`, `bindingToInterfaceEntry`, `classDeclToInterfaceShape`, `interfaceDeclToInterfaceShape`, `aliasDeclToInterfaceShape` | `src/cli/src/detail/toolchain/interface_hash.hpp` (`interfaceHashOf` :86), impl `src/cli/src/toolchain/runtime/interface_hash.cpp`; "The ExportCollector is NOT wired up yet" at :13-16 of the header | This design wires it, per module, at patch time (section 7.3). No changes to its algorithms. |
| Event loop, one per thread (D71); macrotask boundary; `setDrainHook` fires once at loop end; `runOnce()` for tests; `CycleCollector::collect()` at the macrotask safe point | `src/runtime/src/async/eventloop.cpp` :389-420 (`run`), :398 (`drainMicrotasks()`), :159 (`setDrainHook`), :417 (drainHook call), :230-273 (`runOnce`), :268-271 ("macrotask safe point" comment + `CycleCollector::current().collect()`), :437 (`runEventLoop`); `include/cp3/async/eventloop.hpp` :246 | The safe point hooks `run()` right after line 398; `runOnce()` gets the same hook for deterministic tests (section 8). |
| The watcher: 25 ms debounce, stat fingerprint + content FNV-1a confirmation; OLD child keeps running during the rebuild; restart only after a newer exe is ready; compile error keeps the last good app | `src/cli/src/commands/run/dev.cpp` :65 (kDebounceWindow), :689-739 (`rebuild`), :699 (`cmdBuildOrRun(session.opt, false)`), :724-740 (exe-mtime gate + restart), :666-676 (`spawnChild`) | Hot patch inserts a decision between the rebuild call and the restart (section 7). The fallback path is unchanged. |
| Runtime backing shared by all module TUs (`console.hpp` async input pump, timers, promise, cycle collector) | `src/std/console/runtime.cpp`, `runtime_pump*.cpp`; `src/runtime/src/async/*`; `src/runtime/CMakeLists.txt` | Everything becomes ONE shared dev lib (`cp3runtime`) loaded once; one event loop instance (section 3). |

Baseline metric (the v0.2.5 gate): `benchmarks/cases/compute/dev-reload/`
(README.md, 2026-11-13): cp3 2751.8 ms median per edit (15 runs), target the
v0.2.5 fast path; the client-side figure in flight is ~2.1 s. Target after
this work: ~100-300 ms median.

---

## 2. Non-negotiable contract (restated as design invariants)

1. **Release unchanged.** Incremental is the only profile that gains the
   shared-module layout. Release/Debug keep the single-TU direct-call static
   build. `--release` object on disk and every release byte of output are
   untouched. D56 ("the shipped artifact is exactly one executable") is read at
   release scope: `.cp3/dev/**` is not a shipped artifact and is not produced
   by release builds; the dev exe is documented as dev-only tooling output
   under `.cp3/out/` (like the current dev exe, just thinner).
2. **EA unchanged.** No optimizer change, no codegen semantic change; fuel for
   -O0 is the dev contract (incremental profile already selects `-O0 -g0`,
   `build.cpp` :106-109).
3. **D9 / D62 preserved.** No user-observable output change; the event loop,
   microtask FIFO, promise job order and (deadline, insertion id) timer order
   are untouched. The swap never interleaves with a macrotask (section 8).
4. **No new user flags.** The mechanism is automatic for Incremental builds.
   Escape hatches exist only as environment variables for test fixtures
   (section 12), never as CLI surface.
5. **No second runtime.** Dev uses the SAME runtime TUs compiled as one shared
   lib. The release runtime is the same source, compiled static.
6. **Determinism.** Same inputs, same artifacts: table order, state layout
   offsets, descriptor bytes and file names are all pure functions of the
   source (sections 4-6). The build is reproducible modulo compiler timestamps.
7. **Fail-safe.** Every failure mode degrades to the Phase 3 behavior: the old
   app is kept alive; the user sees the same diagnostics they would today
   (section 9).

---

## 3. The dev runtime/model split

### 3.1 What moves into the shared dev runtime (`cp3runtime`)

Exactly the same translation units the Incremental build compiles today
(`runtimeSourcePaths`, `src/cli/src/toolchain/runtime/util.cpp` :379), built
once as a shared library per (toolchain, runtime sources, dev profile) and
cached by content stamp under `.cp3/dev/`:

| TU (from `util.cpp` `kRuntimeUnits` :326-343) | Contents | Dev role |
| --- | --- | --- |
| `src/runtime/src/core/runtime.cpp` | `cp3::runtime::toString` (D9 formatter), `print`/`println`, `name()`/`version()` | shared lib |
| `src/runtime/src/core/error.cpp` | `cp3::Error` | shared lib |
| `src/runtime/src/core/cycle.cpp` | `CycleCollector` | shared lib |
| `src/runtime/src/core/regex.cpp` | PCRE2-backed regex | shared lib (needs PCRE2 static, linked into the module link wherever the static runtime would have been) |
| `src/runtime/src/core/bucket_hash.cpp` | hash backing for collections | shared lib |
| `src/runtime/src/async/eventloop.cpp` | the one event loop (D71/D62) | shared lib, with the new safe-point hook (section 8) |
| `src/runtime/src/async/promise.cpp` | `Promise<T>`, await, unhandled rejection (D43) | shared lib |
| `src/runtime/src/async/timers.cpp` | `setTimeout`/`setInterval`/`clearTimeout` (D62.2-3) | shared lib |
| `src/runtime/src/async/uv_loop.cpp` | `std/network` libuv binding (D71) | shared lib (only when `std/network` used, per the existing marker selection) |
| `src/runtime/src/parallel/parallel.cpp` | the `parallel.*` pool | shared lib (workers never run in a module lib; they link statically, section 5.3) |
| `src/std/console/runtime.cpp` | console backing (D49) | shared lib |
| `src/std/console/runtime_pump.cpp`, `runtime_pump_read.cpp` | async console input pump | shared lib |
| and the other std family TUs selected today by the same marker list (`crypto`, `process`, `io`, `network`, ...) | per D72/D68 | shared lib, same selection |

Plus ONE NEW TU (section 7): `src/runtime/src/dev/hotpatch.cpp` (with
`src/runtime/include/cp3/dev/hotpatch.hpp`), implementing the host-side
registry, safe point, patch load/verify/swap and the retirement list.

**One runtime instance is mandatory, not optional.** If module libs each linked
a static runtime, each would carry its own `EventLoop`, `Promise` state and
`CycleCollector`, and D62/D71 would be observably broken (a timer registered in
module A would be driven by A's loop while `runEventLoop()` in the entry lib
drains a different one). The shared `cp3runtime(.dll|.so)` is linked by the
exe AND by every module lib (as an import by soname loaded once by the exe;
section 11), so exactly one copy of every runtime object exists per process.

### 3.2 What stays in the exe

The exe contains ONLY:

1. `cp3_program_bootstrap.cpp` (generated entry TU, dev shape): the thin
   `main()`. It locates `.cp3/dev/`, loads `cp3runtime` and every module lib in
   `compilationOrder()` order, verifies each descriptor (section 4), runs the
   module initializers through the tables (or, for a one-shot `cp3c run`, the
   startup sequence), runs the entry module's initializer, then calls
   `::cp3::runtime::runEventLoop()` and `CycleCollector::current().drain()` -
   exactly the tail today's `appendProgramMain` emits
   (`program.cpp` :125-132).
2. `cp3::dev` registry storage and the safe-point glue (these live in
   `cp3runtime`; the exe calls them).
3. Nothing of user code. Every checked unit, including the entry unit's body,
   lives in a module lib. The exe therefore never needs relinking for any
   body-only user-code edit; only `cp3_program_bootstrap` (a stable, tiny
   generated file) can force an exe relink, which happens when the module
   graph itself changes shape (new module, renamed file, new import) - i.e.
   the same events that today change the manifest's shared header.

### 3.3 Layout under `.cp3/dev/`

```
.cp3/dev/
  cp3runtime.dll           (POSIX: cp3runtime.so, soname "cp3runtime.so")
  cp3runtime.stamp         compiler key | runtime source fingerprint | ABI version | dev profile
  registry.json            { compilerKey, runtimeAbi, entry, modules: [...] } (section 7.6)
  build-manifest-dev.txt   this tree's own manifest (v1 format, separate from .cp3/build/)
  patch/                   patch handshake area
    <gen>/patch.json       per-generation manifest (a refused generation is left unused, never retried)
    pending                atomically-renamed marker (section 7.4)
    applied-<gen>.json     the app's ack for generation <gen>
    refused-<gen>.json
  modules/
    <module-stem>.<gen>.dll    one file per checked unit per generation; old
                               generations stay mapped until process exit (section 7.7)
```

`.cp3/dev/` is never read by release code paths and is removed by `cp3c clean`
alongside `.cp3/build/` and `.cp3/out/` (one-line doc update, section 13).

### 3.4 First-time cost

The first dev build in a project compiles `cp3runtime` once (unchanged total
work vs today's static link of the same TUs, plus one `-shared` link).
Subsequent dev runs reuse it via `cp3runtime.stamp`. A body-only edit never
pays this again. Honest first-build cost: ~2-4 s on the reference machine;
documented, dev-only, one-time per (compiler, runtime change).

---

## 4. The module descriptor ABI

### 4.1 Uniform discovery, not mangled per-module names

Every module lib exports exactly one uniform name (extern "C", no mangling):

```
extern "C" const cp3_module_descriptor_v1* cp3_get_module_descriptor(void);
```

Justification over `cp3_module_table_<mangledmodulename>`: the loader knows
the handle it holds and the expected stem from `.cp3/dev/registry.json`
already, so a known name is always sufficient; one name works with
`LoadLibraryW` + `GetProcAddress` and `dlopen` + `dlsym` unchanged, survives
any C++ name mangling scheme and any module name charset (path-derived stems
are not guaranteed valid C identifiers on every platform), and keeps the
discovery code identical in dev and in any future tooling. The mangled variant
was considered and rejected: it buys nothing over the handle, and it makes
Windows MinGW export tables fragile (long decorated names, case-insensitivity
edge cases).

### 4.2 Export surface of every module lib

```cpp
// New PRIVATE declarations for the generated descriptor. The authoritative
// struct definitions live in cp3/dev/hotpatch.hpp (runtime-side, new file);
// the codegen emitter's rendering of them lives in
// src/codegen/src/detail/generator/dev/state_map.h (new file, section 13).
extern "C" {
const cp3_module_descriptor_v1* cp3_get_module_descriptor(void);

// The function table, filled by value into caller storage. Order: the
// canonical order defined in 4.4. `entry_count` must equal the descriptor's
// `table_count`. Returns false on count mismatch.
bool cp3_module_fill_table_v1(void** out_entries, std::uint32_t entry_count);

// Construct/destroy the mutable state block in-place. `block` has the size
// and alignment from the descriptor. `init` value-constructs every field
// (no user code runs); `destroy` runs field destructors. Neither runs any
// top-level statement - initializers stay in cp3_module_init_v1 (6.3).
void cp3_module_state_init_v1(void* block);
void cp3_module_state_destroy_v1(void* block);

// The module initializer, exactly today's namespace-scope cp3_module_init(),
// re-exported uniformly. Non-top-level-await units only.
void cp3_module_init_v1(void* block);

// Informational duplicates of the descriptor constants, for objdump/dumpbin
// checks and tooling. The descriptor is authoritative; the host never reads
// these.
const std::uint64_t cp3_module_interface_hash_v1;
const std::uint64_t cp3_module_abi_compiler_key_v1;
const std::uint32_t cp3_module_abi_runtime_v1;
const std::uint64_t cp3_module_state_layout_hash_v1;
}
```

`std::uint64/32_t` in the public ABI block is fine: the file is C++20 and is
never compiled as C. Nothing in the ABI depends on C++ names, vtables or
layouts; `cp3_module_descriptor_v1` is a POD with fixed-width fields and an
explicit `struct_bytes` for forward compatibility.

### 4.3 The descriptor struct (C-ABI, POD)

```cpp
struct cp3_state_field_v1 {
  const char*  name;        // canonical binding name, NUL-terminated, static
  const char*  cpp_type;    // canonical type text (typeToCanonicalText), static
  std::uint32_t offset;     // bytes from block base
  std::uint32_t size;       // sizeof(cpp_type)
  std::uint32_t align;      // alignof(cpp_type)
};

struct cp3_module_descriptor_v1 {
  // Header
  std::uint32_t struct_bytes;     // sizeof(cp3_module_descriptor_v1); versioning
  std::uint32_t struct_version;   // == 1
  std::uint32_t abi_runtime;      // CP3_RUNTIME_ABI_VERSION (new constant, == 1)
  std::uint32_t flags;            // bit0: has top-level await (host must refuse);
                                  // bit1: has workers (host must refuse)
  // ABI tag: the triple that must match the exe exactly.
  std::uint64_t interface_hash;   // fnv1a64 over serializeInterfaceSignature (4.5)
  std::uint64_t compiler_key;     // fnv1a64 of the compiler key string
                                  // (family|version|driver path|target|sysroot)
  // State block layout (6.2). state_size == 0 for a module with no mutable
  // module-level bindings; state_fields == nullptr then.
  std::uint64_t state_layout_hash;// fnv1a64 over canonical "name type off size align\n" lines
  std::uint32_t state_size;
  std::uint32_t state_align;
  std::uint32_t state_field_count;
  const cp3_state_field_v1* state_fields;  // static array inside the lib
  // Function table
  std::uint32_t table_count;      // number of callable exports; 0 for pure-state modules
  // (table itself is NOT stored here; the host calls cp3_module_fill_table_v1)
  // Identity (informational; the registry carries the same)
  const char*   module_name;      // logical module name, e.g. "src/counter"
  const char*   namesp;           // e.g. "cp3_mod_counter"
};
```

All pointer fields reference static storage inside the lib, valid until process
exit (module libs are never unloaded before exit in v1, section 7.7). No field
is ever null-checked by the host without a version gate on `struct_version`.

### 4.4 Function table: exact contents and order

The table holds one function pointer per **callable export** of the module, in
this exact deterministic order:

1. Collect the export surface after sema (the same `ExportSurfaceMap` the CLI
   builds today in `pipeline.cpp` :37-46 from `functionBinding` :83-101).
2. Keep entries whose `ImportedBinding` `isFunction == true` (functions, async
   functions, generic functions). Class exports are NOT callables here (their
   methods live with the class; class bodies are interface, section 12).
   Default-export functions are callables. Exported value/const/type/class/
   interface/alias entries are excluded.
3. Sort by export name, byte-wise ascending over UTF-8 bytes; a default
   export sorts under the literal name `default` (it does not receive special
   placement beyond that).
4. The index of each callable in this sorted order is its table index, forever,
   for every generation of the module. The host records `table_count` only;
   per-name indices are derivable by the codegen that emitted the call site
   from the same surface, which is why the table needs no names.

Each table entry has signature:
```
Ret fn(void* __cp3_state /* implicit first parameter */, Args...);
```
(uniformly the state pointer first, then the cp3 parameter list; see 6.2 for
why this particular convention was picked.)

### 4.5 The three ABI facts and where they come from

| Fact | Computed how | Used for |
| --- | --- | --- |
| `interface_hash` | `interfaceHashOf(entries, classes)` from `interface_hash.hpp` :86, with `entries` = the module's export surface flattened by `bindingToInterfaceEntry` :116 (named + default) and `classes` = the module's class/interface/alias declarations via `classDeclToInterfaceShape` :143 / `interfaceDeclToInterfaceShape` :155 / `aliasDeclToInterfaceShape` :162. Entry creation happens exactly where `analyzeProgramGraph` already builds `ExportSurfaceMap` (`pipeline.cpp` :296-360); the existing `ExportCollector` gap noted at interface_hash.hpp:13-16 is closed by calling the same builders from the patch path. | Refusal detection: any export signature/type/shape change (functions, classes, interfaces, aliases, exported values) marks the module's surface "changed". Body changes do not affect it. |
| `compiler_key` | `fnv1a64` of the exact compiler key string the dev exe was linked with (`clang|23.1.4|<driver path>|--target=...|--sysroot=...`, the same string recorded per manifest entry as `compiler` in `.cp3/build/build-manifest.txt`) | A toolchain upgrade between patches refuses the swap rather than mixing code built by two compilers in one process. |
| `state_layout_hash` | see 6.2 | Any change to the module-level mutable binding set (add/remove/rename/retype) refuses the swap. |

The "ABI tag" of the v0.2.5 contract is the triple
(`interface_hash`, `compiler_key`, `CP3_RUNTIME_ABI_VERSION`). The host checks
all three; a single mismatch refuses (section 9). All three are stable under a
body-only edit by definition: `interface_hash` changes exactly when the export
surface changes, `compiler_key` changes exactly when the toolchain changes,
and `CP3_RUNTIME_ABI_VERSION` changes exactly when the runtime ABI changes.
Any one of them changing means the process cannot safely host new code built
now, so the swap is refused rather than attempted.

### 4.6 What the host does with it at startup (fresh process)

1. Reads `.cp3/dev/registry.json` (written by the CLI after every build; see
   7.6) for `compilerKey`, `runtimeAbi`, and the ordered module list
   (`compilationOrder()` with `isPrelude && !isEntry` units skipped, exactly
   the filter in `buildAssembly` :416-421).
2. For each module, in order: `LoadLibraryW`/`dlopen` (absolute path), resolve
   `cp3_get_module_descriptor`, verify `struct_bytes`/`struct_version`,
   `abi_runtime`, `compiler_key` (constant, cheap), `interface_hash` vs the
   recorded value, state triple (`size`/`align`/`layout_hash`/`field_count`
   and per-field `(name, cpp_type, offset, size, align)`), `table_count`.
3. On any mismatch: print a fatal dev error naming the module, the field, and
   the expected/actual values; `exit(3)`. This is not a user error; it means
   the `.cp3/dev/` tree is inconsistent (interrupted write, IDE restored from
   backup). The message tells the user to rerun with `cp3c clean`. Release is
   unaffected.
4. On success: allocate the state block, `cp3_module_state_init_v1`, fill the
   table via `cp3_module_fill_table_v1`, publish an immutable snapshot into
   the host registry (5.4), and store `(handle, generation)` in the retirement
   list pre-loaded with generation 1.
5. Run initializers in order (every checked unit's `cp3_module_init_v1`, in
   `compilationOrder()` order, called directly with its own state block),
   then the entry module's init, then today's main tail.

Startup load is also what makes `cp3c run` (one-shot) able to reuse the same
artifacts; the perf path is the patch path (section 15).

---

## 5. The module library build

### 5.1 TUs per module lib

Exactly ONE TU: that unit's `cp3_unit_N.cpp`, i.e. the same file
`unitModuleSource` (`program.cpp` :336) produces today, plus the new
descriptor section emitted into it (a new section at the end of the unit TU,
generated when `profile == Incremental`; see 13). No other unit, no runtime
TU, no PCH TU. Link: `-shared` (POSIX: `-shared -fPIC`; Windows/MinGW:
`-shared` with `-Wl,--out-implib=` discarded), `-O0`, no LTO (the Incremental
profile already pins `-O0 -g0` and no LTO/native-ISA/unroll,
`build.cpp` :103-109), with the same include roots and `foreignFlags` the
current plural build uses (`.cp3/build/` include roots, `nativeFlags`, and
the vendored archives only if the unit actually needs them; crypto/network
module libs link their vendored static backends exactly as the static build
does today - they are baked into the module lib, they do not share a lib).

### 5.2 Flags (exact)

Common: `-std=c++20`, `-O0 -g0`, no LTO, `-fvisibility=hidden` (both
platforms; clang honors it for MinGW too), the PCH via
`-include-pch .cp3/build/_cp3_pch.pch` (Incremental only) when
`buildOrReusePch` succeeded, else no prefix.

POSIX additionally: `-fPIC -shared -Wl,--no-undefined` and `-soname` NOT set
(the module lib is never found by soname, only by absolute path).

Windows (clang MinGW) additionally: `-shared` and explicit exports via the
descriptor's `visibility("default")` attributes; `-Wl,--no-undefined` is
supported by lld/GNU ld and is used. If a future toolchain rejects
`--no-undefined`, fall back to a `/DEF` file generated next to the lib with
exactly the 9 `cp3_*` exports; the design does not depend on which is used,
only that unresolved symbols fail at module link time, not at the safe point.

### 5.3 Workers and parallel units

`new Worker(...)` units (spec 13, D80) are NOT module libs. They are separate
graphs compiled as standalone TUs and linked STATICALLY into the exe as today
(`prepareWorkers`, `run.cpp` :221-245). A program using workers is entirely
excluded from hot patch (`flags & bit1` in the descriptor; the CLI detects
`WorkerSite`s in the graph before patching and skips with reason
`workers-present`). This is the only sane v1 boundary: workers capture
`.text` addresses freely and run user code on threads that never pass through
the event loop.

### 5.4 Host-owned tables; snapshots, not in-place memcpy

The host keeps one table per module in registry memory:

```cpp
struct cp3_dev_table_snapshot_v1 {
  void* state;                  // never changes for the module's lifetime
  std::uint64_t generation;     // 1 for the startup load; +1 per swap
  std::uint64_t entry_count;
  void* entries[];              // POD function pointers, canonical order; flexible array
};

// Registry slot (host, process-wide):
std::atomic<const cp3_dev_table_snapshot_v1*> table{nullptr};
```

**Swap = one atomic pointer flip to an immutable snapshot.** The old table
memory is owned by its snapshot; nothing mutates it in place.

Justification vs `memcpy` under a seqlock or per-entry atomic stores:

- A seqlock forces a validate-retry on EVERY cross-module call on every call
  site forever (branch + generation load + retry), and a reader torn mid-swap
  must retry; the cost model is worse than one acquire load and it leaks into
  the generated codegen of every dev build.
- Per-entry atomic stores (`std::atomic<void*> entries[]`) make every reader
  pay an atomic load per entry and still give no atomicity ACROSS entries:
  caller A can observe entry 3 from generation 2 and entry 4 from generation 3
  in the same call sequence. Function-pointer blocks are small (tens of
  bytes) but the hardware gives no multi-word atomicity guarantee; the
  seqlock/per-entry approaches exist to paper over this, and both are
  strictly worse than the flip.
- The flip gives every reader ONE linearizable view of the whole table for
  the whole duration of its call; the old snapshot is a valid, fully-readable
  object after the flip because it is never freed before exit (7.7). The cost
  per cross-module call is one acquire load + one indirection - identical in
  shape to today's direct call plus the state-pointer register.

`state` lives in the snapshot because callers need both; `generation` exists
for diagnostics and the retirement list. A `const cp3_dev_table_snapshot_v1*`
acquire-load is all the codegen emits at a cross-module call site
(section 6.4).

### 5.5 Publication of the patch (where the new lib file lives)

Libs for generation `g+1` of module `M` are written to
`.cp3/dev/modules/.<module-stem>.<gen>.tmp.dll`, fsynced/closed, then renamed
into `.cp3/dev/modules/<module-stem>.<gen>.dll`. The name is unique per
generation and a partial write is never a shippable lib (the rename is after
the close). The CLI then publishes `.cp3/dev/patch/<gen>/patch.json` (temp +
rename), then atomically renames a `pending` marker into place. The app only
ever opens libs via absolute paths under `.cp3/dev/modules/`, and the marker
rename is the single atomic commit point of the whole protocol (7.4). Partial
patch state is impossible: either the marker does not exist (nothing was
attempted; old app untouched), or it exists and names exactly one complete
generation whose lib file is already fsynced at its final path.

### 5.6 `-fvisibility=hidden` discipline

Module libs: only the 9 `cp3_*` exports are visible; every `cp3_mod_*` C++
symbol and every template instantiated in the lib stays local. This is what
makes `dlsym`/`GetProcAddress` collisions between generations impossible and
what keeps the OS loader's symbol search cheap. The same discipline on
`cp3runtime` would hide the runtime ABI the module libs need; `cp3runtime`
exports everything (it is built with default visibility; hidden-by-default
there would require a `/DEF`-style export list and provides no additional
safety for a dev-only artifact).

---

## 6. State

### 6.1 Which bindings go where

| Module-scope binding | Placement | Why |
| --- | --- | --- |
| `let`/`var` (exported or not), destructured mutable bindings | **state block** | Mutable; must survive a code swap. |
| `const` with a static-init initializer (literal/constant-folded, `isStaticInitExpr` in `module_var.cpp` :39) | const namespace-scope global **in the lib** | Immutable; the old and new libs may each keep a copy; no identity is observable. |
| `const` with a non-static initializer (e.g. `const x = compute()`) | lib-local namespace global (dynamic init in the lib, as today) | Immutable forever; each generation gets its own copy; JS init order is preserved because the initializer runs in source position among the module's top-level statements (6.3). |
| function declarations (incl. default export) | lib `.text`; table entry if callable export | Code, swapped. |
| classes, interfaces, enums, type aliases | lib + re-emitted per consumer namespace (as today) | Interface: any change is an interface change (section 9). Class bodies are NEVER hot-patched (section 12). |
| `$o{...}` value structs (guarded globals) | lib (as today) | POD data, re-emitted identically. |
| closures captured by escaping callbacks | heap `Cell<T>` (spec 10) | Not module state; unaffected by swap. |

Exported mutable `let` bindings are read/written by other modules through the
host: the consumer emits `cp3::dev::stateBlock(<calleeModuleId>)->field_<n>`
in dev (one extra indirection to a process-constant pointer), never through
its own TLS.

### 6.2 The state block and the layout hash

Per module, in declaration order of mutable bindings (NOT sorted; see below):

```cpp
// Conceptual layout (offsets computed by codegen, not stored in source):
struct /* generated, never named in source */ {   // POD, only the fields below
  double       counter_;    // let counter = 0;        offset 0,  size 8
  std::string  name_;       // let name = "cp3";       offset 8,  size 32 (libstdc++ ABI)
  Ref<Note>    head_;       // let head = null;        offset 40, size 16
};                                                 // size 56, align 8
```

`state_layout_hash = fnv1a64` over canonical lines, one per field, in
declaration order:

```
<name> <typeToCanonicalText(type)> <offset> <size> <align>\n
```

plus a trailing line `size <state_size> align <state_align> count <n>\n`.

Design choices, justified:

- **Declaration order, not sorted order.** Offsets then match the source
  author's mental model, and the layout hash changes for exactly the edits
  that should force a rebuild: add/remove/rename/retype a mutable binding, or
  reorder them (a swap would still be memory-safe but the author cannot
  reason about offsets across generations, so we refuse; the interface check
  would not catch this one, so the layout check is NOT redundant with the
  interface hash). This is why the two checks are both mandatory.
- **Type text in the hash.** `let x: number` -> `let x: boolean` keeps the
  offset but changes semantics; `typeToCanonicalText` catches it.
- **Exported or not, all mutable module-scope bindings are covered.** The
  interface hash only sees exported ones; non-exported `let` changes are the
  common case for a body-only edit that adds scratch state, and they must be
  classified as layout changes (rebuild) or the state corruption would be
  silent.
- **Block is host-owned.** The host allocates it once per module at the first
  load (or first patch refusal recovery), never inside a module lib, so a
  code swap cannot invalidate it. `cp3_module_state_init_v1` value-constructs
  fields in place (no user code), `cp3_module_state_destroy_v1` destroys them
  at process exit.

### 6.3 Initializer order is preserved exactly (output byte-identity)

State storage (`_init_v1`) never runs user code; the *initializer statements*
stay in the module's `cp3_module_init_v1` body exactly where today's
`cp3_module_init()` carries them, in source order, reading and writing
`__cp3_state`. `let x = compute()` is therefore emitted as

```cpp
// in the descriptor/state section (no user code):
__cp3_state->x = <value-initialized>;     // via cp3_module_state_init_v1
// in cp3_module_init_v1 (in source position among the statements):
__cp3_state->x = cp3_mod_counter::compute();
```

so side-effect order vs release is identical, which is what D9-byte-identical
stdout requires of `let x = sideEffect()` sequences. (Release keeps today's
in-place global initialization; both produce the same byte stream.)

### 6.4 How functions reach the block: ONE chosen convention

**Every module function (including non-exported ones) takes an implicit first
parameter `void* __cp3_state`**, in both its declaration section and every
definition. Table-indirected call sites pass `table->state`; same-module call
sites pass the state pointer already in hand; the entry module's functions
receive it from the host. Cross-module state access (an exported `let`) goes
through `cp3::dev::stateBlock(moduleId)` as in 6.1, which resolves to the
host-owned block.

Justification vs the TLS-like `__cp3_state` pointer alternative:

- **No initialization-order hazard.** TLS would need a defined write of the
  per-thread pointer before EVERY first call into the module from every
  thread (the loop thread, worker threads that do call into module libs for
  tuple-lifted shared helpers, and any thread created later). With the
  implicit parameter, every call carries its own block pointer; there is
  nothing to forget and no window where a fresh thread sees an unset pointer.
- **No free at unload temptation.** With DLLs unloadable in principle, TLS
  slots on Windows are invalidated; block-passing is immune.
- **In-flight correctness for free.** A call that began before a swap
  captured the block pointer at its call site; it cannot observe a "half
  swapped" or wrong-generation state pointer, because the block never moves
  and the pointer is copied by value into the callee frame.
- **ABI checkability.** Table entries share one uniform leading-parameter
  convention; the host verifies `table_count` and the module init signature at
  load without demangling anything.
- Cost: one extra register argument at -O0. Irrelevant in dev; absent in
  release (release never emits the parameter).

Rejected alternative documented for the record: TLS
(`thread_local cp3_state* __cp3_state` read in each prologue). It removes the
parameter from the call chain but adds an always-loaded TLS object, an init
ordering problem per thread, and a per-generation stale-pointer window with no
compensating win.

**Pipeline plumbing** (dev only): `Generator::runUnit` emits module-scope
mutable bindings through the state map; the declaration of every module
function gains the leading `void* __cp3_state` parameter; `module_var.cpp`'s
namespace-global lowering for mutable bindings is bypassed in dev (the
pattern-binding path does the same); function *pointers* stored into module
state (e.g. `let cb = () => ...`) are fine: they are heap `Cell<T>`-ish typed
`Cell`s of `std::function<void(void*, ...)>` in dev, layout constant.

### 6.5 Static const data stays in the lib

Literals, string tables, regex tables, and any `static const` POD/arrays live
in the module lib's `.rodata` as today. Readers in the same TU bind them
statically forever; readers from other units never reference another unit's
`.rodata` (the link table only forwards names), so lib generations do not
share mutable/const blobs. Unaffected by swap; never freed early (7.7).

---

## 7. The swap protocol, step by step

Notation: `M` is the module that changed. `gen` is a monotonically increasing
generation counter per project (persisted in registry.json).

### 7.0 Detection (CLI, `dev.cpp`)

Unchanged: OS event -> debounce (25 ms) -> stat fingerprint -> content FNV-1a
confirm. A no-content save never schedules anything. On a real change
(`rebuild(session, false)`, dev.cpp :689), the pipeline begins as today with
ONE interception:

```
if (opt.compile.profile == Incremental && hotpatchEnabled()) {
  HotPatchOutcome o = tryHotPatchRebuild(session, changedFiles);
  if (o.isApplied())     { noteApplied(o);  return; }   // keep child, no restart
  if (o.isRefused())     { renderRefusal(o); goto fallbackPhase3; }
  if (o.isNotApplicable()) { noteReason(o);  goto fallbackPhase3; }   // interface change, workers
  if (o.isBuildError())  { /* diagnostics already rendered, old app alive */ return; }
}
fallbackPhase3: // exactly today's code path below dev.cpp:699
```

`tryHotPatchRebuild` never rethrows into `cmdBuildOrRun`'s control flow; it is
a separate function in the new `src/cli/src/detail/hotpatch/` tree (section 13).

### 7.1 Identify M and classify the edit

1. Re-derive the watched set content diff (`contentFingerprint` diff) to list
   changed files. (The graph is rebuilt anyway in the patch path, so this is
   cheap and exact.)
2. Load the graph fresh (the same `loadModuleGraph` the normal path uses);
   map each changed file to its `CompilationUnit`.
3. If the changed unit set is not exactly one checked unit whose source hash
   differs, or the graph topology changed (new imports, renamed units,
   prelude membership change, package set changed): `NotApplicable{topology}`
   (the shared header must change; the exe must relink). The entry module is
   NOT excluded: it is an ordinary module lib after startup like any other
   unit, so a body-only edit to it is a candidate like any other. Its
   top-level statements ran once at process start and are not re-executed on
   a patch (10.4); everything else in it (functions, timers, promises)
   swaps normally. Note the one-shot `cp3c run` path never observes a patch
   at all (each run is a fresh process); the patch path exists for the
   long-running `cp3c dev` app.
4. Re-sema the changed unit (incremental frontend; see 15 for the hard
   dependency on the phase-3 incremental sema and its fallback estimate).
   Sema errors in M: `BuildError` (diagnostics rendered, exit path identical
   to today's compile error; the old app stays alive; the user sees exactly
   the errors `cp3c check` would print).
5. Compute M's new interface surface (`ExportSurfaceMap`, as in
   `pipeline.cpp` :296-360) and `newInterfaceHash = interfaceHashOf(...)`.
   Compare to the value recorded in registry.json for M.
6. Compute M's new state layout (6.2) and `newStateLayoutHash`; compare.
7. Classification:
   - `newInterfaceHash == recorded && newStateLayoutHash == recorded` AND
     no unit in the graph has top-level await AND no `WorkerSite` in the
     graph: **body-only candidate**.
   - otherwise: `NotApplicable{interface|statelayout|toplevel-await|workers|topology}`.

### 7.2 Build ONE module lib

1. Write `.cp3/dev/patch/<gen>/<stem>.<gen>.cpp` = the new unit TU text
   (`unitModuleSource`), with the descriptor section included.
2. Invoke clang exactly as the plural pipeline does for this unit's object
   job, plus `-shared -fPIC -Wl,--no-undefined -fvisibility=hidden` and output
   through the atomic publish path of 5.5 (`.tmp` then rename into
   `modules/<stem>.<gen>.dll`; one compile, one link; the CLI already knows
   how to spawn clang with the right flags; this is one job, not the pool).
3. Any failure: delete the staged `modules/.<stem>.<gen>.tmp.dll`, return
   `BuildError` (old app alive).

### 7.3 Verify before publishing

Load-time verification happens IN THE APP; the CLI does not run the lib. The
CLI computes and records all expected values in `patch.json`
(`.cp3/dev/patch/<gen>/patch.json`):

```json
{ "generation": 7, "module": "src/counter", "namesp": "cp3_mod_counter",
  "lib": "modules/counter.7.dll",           // final path under .cp3/dev
  "interfaceHash": "13268008235088934565",
  "compilerKey": "13268008235088934565",      // fnv1a64 string form
  "abiRuntime": 1,
  "stateSize": 56, "stateAlign": 8,
  "stateLayoutHash": "9821...", 
  "stateFields": [ {"name":"counter","type":"number","offset":0,"size":8,"align":8} ],
  "tableCount": 3,
  "expectedExportOrder": ["decrement","increment","total"] }
```

The app re-derives everything from the loaded descriptor and compares to this
file; it also compares against registry.json (which it trusts as the last
known-good). Both must agree; disagreement is a refusal.

### 7.4 Publish; the ONLY atomic commit point

1. `fdatasync`/`FlushFileBuffers` on the lib and the directory entries
   changed; write `patch.json` to `patch/<gen>/patch.json.tmp` and rename it
   into `patch/<gen>/patch.json` (per-generation directory); fsync the
   directory.
2. Write `pending.tmp` = `"<gen>"`, fsync, `rename(pending.tmp, pending)`
   (atomic on NTFS and on POSIX ext4/btrfs). **This rename is the commit
   point.** Before it: nothing observable happened. After it: the next safe
   point in the app applies the patch. See section 8.3 for why the wake is
   sound and cannot reorder user code.
3. The CLI now waits for `patch/<gen>/applied-<gen>.json` up to 250 ms
   (polling the result file every 5 ms), then up to 1 s total; on timeout it
   falls back to Phase 3 (kill + full rebuild + restart, section 9, F7).
4. A tiny watcher thread INSIDE the dev exe (started by the bootstrap, not by
   the CLI) polls `.cp3/dev/patch/pending` by mtime every 25 ms (portable, one
   `stat` per poll; see section 8.3 for why this wake channel is sound and
   cannot reorder user code). On change it calls
   `cp3::runtime::currentLoop().postFromThread(Task([]{}))`.
   Rationale: `postFromThread` is public and wakes the loop if blocked in
   `waitForNext()` (it notifies `wakeCv_`, `eventloop.cpp` :22-31); the
   queued no-op task is adopted as a macrotask, runs (does nothing), the loop
   takes another turn, and the safe point fires. D62 is untouched: the task
   carries no user code and cannot reorder timers or promises (8.3).

### 7.5 App-side apply (on the event-loop thread only)

At the safe point (inserted in `EventLoop::run()` between the top
`drainMicrotasks()` at eventloop.cpp:398 and the `pumpReadyTasks()` at :399,
and in `runOnce()` after its second `drainMicrotasks()` at :265; section 8):

1. If the watcher thread flag is unset: one predictable `stat` on `pending`;
   if unchanged or absent, return (one atomic load fast path).
2. Read `<gen>` from `pending`; open `patch/<gen>/patch.json`; load
   `.cp3/dev/modules/<stem>.<gen>.dll` with
   `LoadLibraryW`/`dlopen(abs, RTLD_LAZY|RTLD_LOCAL)`.
3. Resolve `cp3_get_module_descriptor`; check `struct_version`, `abi_runtime`,
   `compiler_key`, `interface_hash`, state triple, `table_count`, and the
   full field array against patch.json AND registry.json. ANY mismatch:
   write `refused-<gen>.json` `{reason, field, expected, actual}`; unload
   NOTHING and move NOTHING (the lib stays on disk unused; the `pending`
   watermark records the refused generation so it is never retried); keep the
   app on the old table; return.
4. Build the new snapshot: allocate `cp3_dev_table_snapshot_v1` with
   `entry_count = table_count`, `state = old snapshot's state` (the block is
   REUSED - this is the whole point), `generation = gen`. Fill `entries` via
   `cp3_module_fill_table_v1(out, count)`.
5. `std::atomic_store_explicit(&registry.slot.table, snapshot,
   std::memory_order_release);`
6. Retire the old snapshot: push `{handle_old, gen-1}` to the retirement list;
   update registry.json (module entry: generation, lib name); write
   `applied-<gen>.json` `{module, oldGeneration, newGeneration, ms: stated}`.
7. Return to the loop. The very next call into M (same macrotask onwards)
   uses the new table.

Cost of steps 2-6: two file reads, one `LoadLibrary`, one array fill, one
snapshot alloc, one atomic store. Measured expectation: 5-20 ms. Loading the
lib runs its static initializers on this thread (never a different one), still
inside the safe point, never inside user code.

### 7.6 Registry (`.cp3/dev/registry.json`)

Written by the CLI on every dev build, and rewritten by the APP after every
successful patch (it is the single source of truth for "what does the running
process have loaded"):

```json
{ "compilerKey": "13268008235088934565", "runtimeAbi": 1,
  "entry": "src/index", "modules": [
    { "name": "src/mathx", "namesp": "cp3_mod_mathx", "generation": 1,
      "lib": "mathx.1.dll", "interfaceHash": "...", "stateLayoutHash": "...",
      "stateSize": 0, "stateAlign": 0, "tableCount": 3,
      "sourceHash": "12403115134045494029" }
  ] }
```

The CLI compares patch-time expectations against registry.json before
publishing (early refusal, cheap), and cross-checks the app's ack against it
afterwards (a confused app that forgot a module fails loudly in the CLI log).

### 7.7 Retirement (keep old code valid forever)

Rule: **an old module lib is unmapped only at process exit in v1.**

Retired snapshots accumulate in `.cp3/dev/modules/` (mapped, unused)
until the next full rebuild after process exit, at which point `clean` or the
dev watcher's next startup deletes them. The budget is explicit: N patched
edits in a dev session leak N old libs; each is typically 50-300 KB, so even
300 edits cost < 90 MB - acceptable for a dev session, and it is the only
sound v1 rule.

Why not "unload at the next safe point when refcount hits zero" (the
alternative the v0.2.5 contract allows):

- **Counterexample A - baked closure in module state.**
  `let handler = () => { console.log("old"); }` at module scope of M. A
  body-only edit anywhere swaps in generation 2, but `handler` in the state
  block holds an `std::function` whose `operator()` lives in generation 1's
  `.text`. No table indirection, no activation count, no safe point will ever
  observe `handler`'s code pointer; unloading generation 1 makes the next
  `handler()` jump into unmapped pages. There is no cheap precise test for
  this; it must be excluded, not detected.
- **Counterexample B - queued timer baked into the old lib.**
  `setInterval(() => tick(), 1000)` compiled by M. The timer's `Task` object
  inside `timers.cpp` holds a code pointer into generation 1. The loop's
  timer heap is not inspectable by generation, and the task carries no
  metadata the hot-patch path may rely on without changing `timers.cpp`'s
  ABI-adjacent behavior. Unloading generation 1 crashes at the next tick.
- **Counterexample C - an `await` suspended inside old code** (not even a
  top-level-await module; an ordinary async function awaiting a timer):
  the continuation lambda may live in the old lib; resuming it after unload
  is UB.

The only precise reclamation without changing these ABIs is
**per-escaping-closure anchors**: every closure that escapes into the runtime
(timer/promise/emitter/worker submission) captures a
`shared_ptr<const GenAnchor>` holding its creating module's retired handle;
the retirement list unloads a generation when its anchor count reaches zero.
This is additive to the async lowering but touches every async capture site;
**it is explicitly out of scope for v1** (documented as future work in 14.2)
to keep the first hot-patch slice shippable and reviewable.

An *activation refcount* (increment at table-dispatch call sites, decrement at
return) is also implemented in v1, but only as a DIAGNOSTIC counter per
generation (visible via a verbose log line and `registry.json.stats`), never
as the unload gate, because it cannot see Counterexamples A/B/C.

### 7.8 ASCII sequence (patch path)

```
 CLI (parent)                                the running app (child, Incremental exe)
 -------------                                ----------------------------------------
 dev.cpp watcher fires
 tryHotPatchRebuild()
   load graph, map changed file -> M
   analyzeProgramGraph (incremental sema)
   compute interface hash + state layout hash
   both hashes equal? yes -> body-only candidate, keep going
   both NOT equal          -> not applicable: full rebuild + restart
   write patch/<gen>/<stem>.<gen>.cpp
   clang -c -shared -O0 (PCH)                 (running app, untouched)
   write patch.json; fsync; rename pending
   poll applied-<gen>.json (250 ms..1 s)
                                               dev watcher thread: pending mtime changed
                                               currentLoop().postFromThread(noop)
                                               loop wakes; safe point:
                                                 verify descriptor vs patch.json + registry
                                                 flip table snapshot (release store)
                                                 retire old lib (kept mapped until exit)
                                                 write applied-<gen>.json
   read applied-<gen>.json
   log "patched src/counter.cp3 in 87 ms; state kept; no restart"
   update baselines (dev.cpp flow) - NO stopChildProcess, NO spawnChild
```

If any verify step fails: the app writes `refused-<gen>.json`, keeps the old
table, and the CLI falls back to the Phase 3 restart path. The user sees the
refusal reason and both hash values in the CLI log. The old app never died.

---

## 8. D62 analysis: why the swap cannot reorder anything

### 8.1 Where the hook lives (exact site)

`src/runtime/src/async/eventloop.cpp`, `EventLoop::run()` :389-420. The
insertion is one line after the opening drain at :398:

```cpp
while (true) {
    drainMicrotasks();                                    // :398  (D62.1 boundary)
    ::cp3::runtime::hotPatchSafePoint();                  // NEW (dev+release stub)
    if (pumpReadyTasks()) { continue; }                   // :399-401
    ...
}
```

plus the same call in `runOnce()` :230-273 after its second `drainMicrotasks()`
at :265 (before the `CycleCollector::current().collect()` at :271), so unit
tests can drive patches deterministically via `runOnce()`.

`hotPatchSafePoint()` is defined in the NEW TU `src/runtime/src/dev/hotpatch.cpp`
(not a frozen header; section 13). In a release build the TU is still compiled
and linked (one unconditional symbol, one branch); it no-ops on a thread-local
"nothing pending" check with a single predictable load + branch. If the lead
wants zero release cost, the call site in `eventloop.cpp` can be wrapped in
`#if CP3_HOT_PATCH` with the define set by the build driver for Incremental;
the byte-identity and EA proofs below do not depend on either choice.

### 8.2 The swap never runs user code, and never under a macrotask

- The hook is called where NO `Task` is executing: after `drainMicrotasks()`,
  before `pumpReadyTasks()`. A macrotask cannot be "in between" because the
  loop is single-threaded and the hook runs on that same thread, strictly
  between task executions.
- The hook itself performs no user code: it only reads two files (when the
  watcher flag is set), optionally calls `dlopen`/`LoadLibraryW` (which runs
  the lib's static initializers - POD zero/constant initialization only, by
  construction of generated code), memcpy's a small entry array, and one
  release-store. It never calls `execute`/`invoke`/`operator()` on a `Task`,
  never touches `microtasks_`, `readyTasks_`, `timers_`, `readyTimers_`,
  `incoming_`, `external_` or `microtaskHead_`.
- Because the queue data structures are untouched, every ordering D62 pins is
  preserved by construction: microtasks drain fully before the next
  macrotask and after each fired timer (:233, :265, :326, D62.1);
  `(deadline, insertion)` order in the timer heap and ready FIFO
  (`timerBefore` eventloop.cpp:306-309, `TimerEntry` :297-301, D62.3);
  zero-delay timers due next turn (ready queue, :33-41); `setInterval`
  fixed-deadline semantics (timers.cpp), clamped delays (D62.4), caught
  rejections marking observed (D62.5) - none of these subsystems is modified,
  and the safe point introduced no new ordering operation.
- **Promise job order** (microtask FIFO): a patch can only be observed at a
  point where the microtask queue is EMPTY (post-drain). Therefore no
  promise continuation is ever suspended mid-drain by a table flip; every
  settled-promise continuation queued before the mark still runs to
  completion before the swap is visible to any new call.

### 8.3 No two macrotasks interleave with the swap; workers cannot observe it

- One thread runs the swap. During the hook, no other macrotask is running
  anywhere (the loop is blocked inside it, by definition of being a sequential
  function). Microtasks drained before the hook are done; the next macrotask
  starts after the hook returns, with the new table.
- Worker threads execute no module libs (section 5.3); they never touch the
  registry. The only cross-thread publish primitive used is the atomic
  release-store/acquire-load pair in `registry.slot.table`, and the only
  cross-thread traffic of the patch itself is the file system
  (pending/applied), which has no happens-before requirements beyond rename
  atomicity.
- The watcher thread sets an atomic flag and queues a no-op task via
  `postFromThread`. That task is adopted in `takeIncomingLocked`
  (eventloop.cpp:197-213) and run as a regular empty macrotask; it performs
  zero user-visible effects, so "an extra empty macrotask appears between two
  real ones" is not observable (no output, no timer/priority perturbation -
  it does not create a timer entry, does not drain microtasks early, and is
  not observable by promise order because it runs outside the microtask
  phase).

### 8.4 Semantics of the patch window (what "applies" means)

Let T be a macrotask boundary. A patch published before T and applied at the
safe point at T changes which code executes for calls made AFTER T. Calls
made before T (including everything in the current macrotask, and every
microtask drained before T) run the old code; their callees are pinned by
already-loaded code pointers (baked or table-loaded before the flip), all
kept mapped. Timer/promise callbacks created before T and already queued run
old code; callbacks REGISTERED after T (e.g. a `setInterval` callback
registered after the patch) run the new code the next time they fire. Output
produced before T is byte-identical to the un-patched program; output after T
reflects the new bodies. This is the honest statement of "hot patch" - it is
the same semantics `nodemon` + HMR would give for a stateless handler swap,
with the caveat that cp3 module state intentionally survives (section 10.4).

### 8.5 Top-level await modules are out of scope

A unit with `topLevelAwait == true` makes `main` itself a coroutine
(`program.cpp` :105-116) and its `cp3_module_init_v1` would be a promise
whose continuations capture code addresses from the old generation. Swapping
its table mid-flight cannot migrate the suspended frame. v1 detects the flag
(it is in the descriptor, `flags & bit0`) and refuses swap for that module
with `NotApplicable{toplevel-await}`; the edit goes through the Phase 3
restart path. The same refusal applies if ANY unit in the graph carries top
level await, because the entry's initializer await them in order - keep it
simple: graph-wide detection.

### 8.6 Byte-identity evidence plan

The differential gate (section 12, G1) runs a scripted corpus under both a
fresh Incremental build with the new source and (a) a running app patched to
the new source, fed the same event script, stdout compared from the patch
point onward, and (b) a fresh release build. Both diffs must be empty for
the corpus subset marked byte-identity-critical (console, string, number,
async). EA invariants are inherited: dev uses the same -O0 contract as today;
no pass in the pipeline is reordered or removed for dev.

---

## 9. Fallbacks and refusal matrix

| # | Trigger | Detection point | Behavior | User sees | Old app |
| --- | --- | --- | --- | --- | --- |
| F1 | Compile error in M | frontend (sema/codegen diagnostics) | Phase 3 verbatim: diagnostics rendered to stderr, no patch attempted | identical to `cp3c check` / Phase 3 output | alive, untouched |
| F2 | Interface hash changed (any export signature/type/class shape) | 7.1 step 5, after re-sema | NotApplicable{interface}: full plural rebuild + relink exe + restart | "interface of src/counter changed; rebuilding (state will reload)" | alive until ready, then restarted (today's path) |
| F3 | State layout changed (add/remove/rename/retype mutable module-scope binding) | 7.1 step 6 | NotApplicable{statelayout}: as F2 | "module state layout changed; rebuilding" | as F2 |
| F4 | ABI tag mismatch: compiler key changed since the exe was linked; or `abi_runtime` != 1 | 7.5 step 3 (app side) AND 7.6 early check (CLI side) | Refuse: keep old app; the dev tree is stale; CLI instructs `cp3c clean` or falls back to full rebuild | "ABI mismatch (expected compiler X, runtime Y; found Z) - refusing patch; rebuilding" | alive |
| F5 | State size/offsets mismatch (defensive; 7.1 should have caught it) | 7.5 step 3 | Refuse (as F4) | as F4 with field detail | alive |
| F6 | Patch lib fails to load / missing symbol / wrong arch / corrupt bytes | 7.5 step 2-3 | Refuse; the lib is left on disk unused; `refused-<gen>.json` is written; the refused generation is never retried | "patch failed to load: <error>; keeping the last good program" | alive |
| F7 | Load succeeded but ack never arrives (app busy, blocked, or wedged) | 7.4 step 3 (250 ms + 750 ms) | Fallback: stopChild + full rebuild + restart (Phase 3) | "patch not acknowledged; restarting" | stopped, rebuilt, restarted (worst case = today's 2-3 s) |
| F8 | Watcher heals late / crash mid-patch | dev.cpp's child reaping (:741-754) | process died or changed; next change rebuilds fully | as Phase 3 | n/a (dead); next build is a normal one |
| F9 | Interface changed in a TRANSITIVE importer's lens (M unchanged, but M's dependent assumed the new surface) | cannot happen: F2/F3 rebuild everything on mismatch; the incremental header already retires dependents via `dependencyHash` (`build.cpp` :555) | as F2 | as F2 | as F2 |
| F10 | `.cp3/dev/` inconsistent (interrupted write, versioned libs pruned by hand) | 4.6 step 3 | fatal dev error, `cp3c clean` instructed | clean message with offending field | n/a (process exits before user code) |

Rules: refusal NEVER kills the old app; refusal never produces a half-swapped
state (either the snapshot flipped fully, or it did not); every refusal path
still compiles a full Phase 3 fallback build so the user is never stuck.

---

## 10. The interface-change path, end to end

### 10.1 What rebuilds, what does not

"Interface" = the module's export surface + state layout. On F2/F3:

1. **All** checked unit TUs are recompiled (the shared `cp3_program.hpp`
   changed for dependents because M's decls section is in it; the existing
   `dependencyHash = fnv1a64(header) : headerFingerprint` machinery at
   `build.cpp` :547-557 already implements exactly this retirement - body-only
   edits are the ONLY edits that leave it identical; interface edits retire
   every unit TU).
2. **The dev module libs** are rebuilt (M's lib against the new header;
   dependent libs unchanged on disk but their dependents' function-table call
   sites recompile into new objects... note: only units whose *own* source or
   the shared header changed are recompiled; dependents' libs are relinked
   against the same cp3runtime but are regenerated if their TU changed).
3. **The exe is relinked** (its bootstrap data embeds the module list and
   expected hashes; the shared header changed, so the exe must be relinked).
   `cp3runtime` is NOT rebuilt (its sources did not change).
4. The old app keeps running until the new exe passes its startup
   verification (4.6); then dev.cpp stops the old child and spawns the new
   one. State does NOT survive this path (it cannot: the layout may have
   changed). This is the honest cost of an interface edit and is identical to
   today's dev behavior plus the per-lib links.

### 10.2 What the user sees

`cp3c: source changed; rebuilding...` as today, then
`cp3c: build ok (exit 0)`, then `cp3c: running ...` and the program restarts
with fresh state - identical to Phase 3, with the only difference being that
runtime object reuse is already warm from previous dev runs. Nothing about
D62 or output changes.

### 10.3 Partially applied interface change is impossible

If the full rebuild itself fails (compile error), the old app is NEVER
stopped and no exe is replaced (Phase 3 guarantees this; `dev.cpp` :724-740
only restarts after `rc == kExitSuccess` and a newer exe). A half-built
`.cp3/dev/` tree is impossible at the exe level because the exe link happens
once, after all TUs compile; the module libs reach disk only via the same
fail-fast path. If the process dies mid-restart, `.cp3/dev/registry.json` is
written only after the new app successfully starts (the app rewrites it after
its own bootstrap verification), so the CLI never believes a dead exe is
healthy.

### 10.4 State preservation contract, explicit

| Edit kind | State survives? | Mechanism |
| --- | --- | --- |
| body-only (functions only) | YES | block is host-owned; snapshot flip reuses it |
| mutable binding added/removed/renamed/retyped | NO (rebuild) | F3 |
| top-level statement changed | NO (cannot re-run; the statements executed once at process start); the patch still swaps the unit's functions; `cp3 run` picks it up on the next fresh launch, `cp3c dev` restart is the correct tool when the top-level behavior itself must change | section 8.5 |
| const binding initializer changed (non-static) | per-generation copy | lib-local, fresh value; user-visible but harmless (immutables) |
| class/interface/alias changed | NO (rebuild) | F2 |

---

## 11. Windows vs POSIX specifics

| Concern | Windows (clang MinGW, the release target) | POSIX (Linux/macOS) |
| --- | --- | --- |
| Load the lib | `LoadLibraryW(absPath)`; verify with `GetProcAddress` on the 9 exports; `FreeLibrary` ONLY at exit (7.7) | `dlopen(absPath, RTLD_LAZY \| RTLD_LOCAL)`; `dlsym`; `dlclose` only at exit |
| Finding `cp3runtime` from a module lib | The exe loads `cp3runtime.dll` FIRST by absolute path (bootstrap); dependent module libs then bind to the already-loaded image (same base name) - no search-order dependence. `AddDllDirectory(.cp3/dev)` is also called as belt-and-suspenders for transitive loads (ucrt/cppwinrt unaffected) | The exe `dlopen`s `cp3runtime.so` with `RTLD_GLOBAL` FIRST; module libs built with `-soname cp3runtime.so` resolve their `DT_NEEDED` to the already-loaded object, no rpath needed. `-Wl,-rpath` with the absolute dev path is set anyway for tools that inspect |
| dlopen'd module and its cp3runtime soname mismatch | either both dev or neither (driver guarantees by construction) | assign `-soname cp3runtime.so` in dev; release never builds the .so |
| Calling convention | All exports `extern "C"`, C++ default `__cdecl`; never `WINAPI`/`__stdcall` anywhere in the ABI; a mismatch fails at `GetProcAddress`/verification, never as a silent stack corruptor | n/a (single convention); `-fvisibility=hidden` + explicit default visibility on exports |
| Symbol visibility | `-fvisibility=hidden` on module libs; only the 9 exports visible; no third-party exports (`libgcc` etc. stay static-crust already) | same plus `-Wl,--no-undefined` at module link time; explicit `--version-script` optional, not required |
| `/DEF` vs `--no-undefined` | Use `--no-undefined` (works with lld-19/bfd); if the driver cannot, emit `cp3_module.def` beside the lib with exactly the 9 lines - the design does not care which | `-Wl,--no-undefined` hard requirement |
| Unique file names per generation | `<stem>.<gen>.dll`; a loaded DLL is locked against delete/rename on Windows, so old generations MUST be uniquely named and are removed only after the process exits (clean or next startup) | `<stem>.<gen>.so`; `unlink` of an open library is fine but we keep the same rule for uniformity and for ASan |
| AV / WFP on `.cp3/dev` | Windows Defender / enterprise AV may scan each freshly written DLL at `LoadLibraryW` (measured 20-200 ms spikes possible). Mitigations: document a `.cp3/dev/` exclusion; note that WFP/AppLocker/WDAC machines may BLOCK unsigned DLLs entirely - the patch load then fails (F6) and the user keeps getting Phase 3 restarts, which is correct but misses the target metric; document this explicitly as a deployment caveat | No equivalent; inotify/fanotify scanners are opt-in. `dlopen` on first load maps the file - page-cache warm from the write |
| Keeping the old lib mapped | unique names + never `FreeLibrary` before exit (7.7) | unique names + never `dlclose` before exit; loader keeps the inode valid regardless |
| Path encoding | `registry.json` and `patch.json` are UTF-8; module stems are file-name safe (ASCII only): the lib stem is the module's filename stem when it is already file-system safe, otherwise `m_<fnv1a64(sourceKey)>`; the exact stem is recorded in the manifest/registry, never re-derived from the path | same rule |
| Address spaces | x86-64 only; no 32-bit support planned (Release has none either) | same |
| ASan | MinGW clang `-fsanitize=address` works but is ABI-fragile; run the ASan gate on POSIX (clang -fsanitize=address,undefined) and treat Windows ASan as best-effort | ASan/LSan: `dlclose` of ASan-instrumented code poisons quarantine metadata but never unmaps; our never-until-exit policy is ASan-clean. LSan flags the retirement list at exit - suppress with `__lsan_ignore_object` or `ASAN_OPTIONS=detect_leaks=0` for the hot-patch ASan job only; report, don't fail |
| MSAN | weak on Windows; not part of the gate | MSAN optional; the swap window adds no uninitialized reads (snapshots are fully initialized by construction) |
| Stack unwinding | not needed across the swap (no exceptions propagate through module boundaries; cp3 throws are ordinary C++ exceptions INSIDE one lib) | same |

---

## 12. Test matrix mapped to the v0.2.5 gates

Legend: G# = gate, fixture under `tests/cases/hotpatch/` (new category;
fixtures are plain `.cp3` projects plus a runner script), F# = fixture dir.
All cases run on Linux + Windows in CI; ASan job on POSIX.

| G | Gate (v0.2.5) | Fixture / method | Pass condition |
| --- | --- | --- | --- |
| G1 | Differential corpus: hot-patched vs fresh build, byte-identical stdout | F1: 40-case corpus (console/strings/numbers/async subset of tests/cases). Each case run twice: (a) `cp3c run` fresh Incremental build of the new source; (b) app already running generation 1 gets generation 2 patched at a scripted safe point, same stdin/timer script | stdout byte-identical from patch point to exit for (a) vs (b); exit codes equal |
| G2 | State preserved across body-only swap | F2: `let counter = 0; setInterval(() => { counter += 1; if (counter >= 3) console.log(counter); }, 10)` plus `export function bump()`; launch, then body-only edit to the interval callback's print format | the printed count continues from the live value (never resets to 0); the callback runs generation 2 |
| G3 | Interface change falls back to restart | F3: rename an exported parameter's type (`function f(x: number)` -> `function f(x: string)`) | no swap; full rebuild+restart; app observes new code at fresh state; CLI logs the interface-mismatch reason |
| G4 | ABI mismatch refused (test-only corruption) | F4: build one module lib with `CP3_HOTPATCH_TEST_ABI_TAG=2` (test-seam env var read by the descriptor emitter; dev-only; refuses to affect release) | app writes `refused-*.json` with field `abi_runtime`; old app alive; CLI logs refusal and falls back to Phase 3 |
| G5 | Compile error keeps old app | F5: save a syntax-broken body into the watched file | old app still running with generation 1; stderr shows the diagnostics; no restart; `dev.cpp` exit unchanged |
| G6 | Atomicity under concurrent safe points | F6: single-threaded loop makes this trivial; assert in a unit test of `hotpatch.cpp` that the safe point is a no-op when `pending` is unchanged, and that two back-to-back patches applied in consecutive turns each flip the snapshot exactly once | no torn snapshot readable (module state counter never regresses within one turn); unit test + G1 fuzz covers it |
| G7 | Error during patch (lib load failure) | F7: publish a `patch.json` naming a lib with a missing `cp3_get_module_descriptor` (empty stub) - produced by deleting the export in a local build | refusal, old app alive |
| G8 | Latency: edit-to-first-new-output in a running app | F8: the `benchmarks/cases/compute/dev-reload` scenario + a long-lived fixture (F8b) with a 10 ms interval; edit callback body; measure publish->first-output-with-new-behavior | median over 25 edits <= 300 ms on the reference machine (section 15); report min/median/p95 honestly; the watcher-debounce floor (25 ms) included |
| G9 | `cp3c run` one-shot edit->run wall clock | F9: dev-reload benchmark pointed at the dev profile; compare against the 2026-11-13 baseline (2751.8 ms) | median <= 300 ms; documented in the benchmark README with the machine label (never invent numbers, D60/D64/D67) |
| G10 | D62 order preserved with patch pending | F10: fixture printing interleavings of (setTimeout A, promise B, setInterval C, queueMicrotask D) with a patch published mid-drain; repeat 100x with randomized edit points | the relative order of A/B/C/D is identical to the un-patched run in every iteration |
| G11 | ASan on the swap path | F11: F2 + F1 under `-fsanitize=address,undefined` on Linux; retirement list ignored at exit (`detect_leaks=0`, section 11) | 0 ASan reports attributable to hotpatch TU; no use-after-free (nothing freed) |
| G12 | Retirement never frees executing code | F12: module-global closure fixture (section 7.7 counterexample A): `let h = () => ...;` body-only edit; then `h()` from the same macrotask and from a later timer | generation 1's .text still mapped at every call; no crash; exit clean |
| G13 | State-layout mismatch refused | F13: rename the `counter` binding (body-only otherwise) | state-layout mismatch path (F3-class refusal, `NotApplicable{statelayout}`); old app alive |
| G14 | Top-level-await module not patched | F14: unit with `await sleep(0)` at top level | NotApplicable{toplevel-await}; phase 3 path; no partial re-init |
| G15 | Workers excluded | F15: same as F2 but with `new Worker("./worker.cp3")` present | NotApplicable{workers}; no table flip; full rebuild path |
| G16 | Byte-identical number printing after swap (D9) | covered by G1 + F16 spot checks (NaN, Infinity, -0, 1e21, 0.1 arithmetic) | exact D9 strings |
| G17 | `.cp3/dev` never read by release | static check: release binary contains no reference to `.cp3/dev` strings; `--release` build log shows no shared-lib step | strings scan + build log inspection in CI |

Poll cadence for the ACK in tests: G8's harness polls `applied-<gen>.json`
(written by the app) so measurement is direct, not wall-guessed.

---

## 13. Implementation order, files touched, estimates, and the riskiest part

All changes are additive; frozen headers (`src/common/include/cp3/**`, stage
public headers) are never modified; new code lives under `detail/**` or new
module-private TUs. Owners: cli (`src/cli/**`), codegen
(`src/codegen/**`), runtime (`src/runtime/**`), build files only at module
boundaries.

New files (all new; nothing moved, no frozen surface reshaped):

| Path | Purpose | Size est. |
| --- | --- | --- |
| `src/runtime/include/cp3/dev/hotpatch.hpp` | dev ABI mirror: `cp3::runtime::dev` registry-facing declarations, `hotPatchSafePoint()`, `stateBlock(moduleId)`, table snapshot struct | ~250 lines |
| `src/runtime/src/dev/hotpatch.cpp` | safe point, `.cp3/dev` discovery, verify, swap, retirement list, `stateBlock()`, `postFromThread` wake integration | ~700 lines |
| `src/cli/src/detail/hotpatch/hotpatch.hpp` | `HotPatchOutcome`, `tryHotPatchRebuild`, registry/patch.json (de)serialization declarations | ~120 lines |
| `src/cli/src/detail/hotpatch/rebuild.cpp` | detection, incremental sema wiring, classification, module lib build (clang -c -shared), publish pending/ack wait, refusal rendering | ~600 lines |
| `src/cli/src/detail/hotpatch/registry.cpp` | registry.json read/write, generation counter, (de)serialization | ~200 lines |
| `src/cli/src/detail/hotpatch/dev_build.cpp` | Incremental profile driver: build `cp3runtime` (stamp), module libs at startup, exe bootstrap generation, manifest-dev v1 | ~700 lines |
| `src/codegen/src/detail/generator/dev/state_map.{h,cpp}` | per-unit mutable-binding state layout (offsets), layout hash, descriptor text emission into the unit TU | ~450 lines |
| `src/codegen/src/detail/generator/dev/dev_call_lowering.{h,cpp}` | lowering of cross-unit calls to table calls + `__cp3_state` threading (consulted from the existing call/decl emit sites behind a per-unit "dev table" pointer set by the plural pipeline) | ~400 lines |
| test fixtures | `tests/cases/hotpatch/**` + runner glue | ~400 lines |
| bench/doc | benchmark README note, this doc's D82/D83 landing in DECISIONS.md | ~100 lines |

Modified files (additive edits only; none frozen):

| Path | Change | Size est. |
| --- | --- | --- |
| `src/runtime/src/async/eventloop.cpp` | TWO call sites: `hotPatchSafePoint()` in `run()` (:398-ish) and in `runOnce()` (:265-ish). Not a frozen header | 2 lines |
| `src/cli/src/commands/run/dev.cpp` | intercept in `rebuild()` between `cmdBuildOrRun` and the restart logic (section 7.0); watcher-thread spawn stays in the child (bootstrap), not here; refusal rendering | ~120 lines |
| `src/cli/src/commands/run/run.cpp` | after the plural codegen (Incremental), route module list to `dev_build::installModuleSet(...)` instead of static exe linking when hot patch is enabled; releases untouched | ~80 lines |
| `src/cli/src/commands/build/build.cpp` | `buildJobs` gains a `-shared` variant for the module jobs behind a driver flag set by `dev_build` (the default static path is byte-for-byte unchanged); `.cp3/dev/` artifacts written by the new driver only | ~100 lines |
| `src/codegen/src/emit/decl/module_var.cpp` | dev path: mutable module-scope bindings lower to state-block fields instead of namespace globals (PA path kept for release) | ~80 lines |
| `src/codegen/src/emit/decl/program.cpp` | `assembleProgramModules` (Incremental only): append descriptor section to each unit TU; emit the manifest entries into a new `ProgramAssembly::devModules` side channel (this is additive: new member on the INTERNAL `ProgramAssembly`, not a frozen type) | ~120 lines |
| `src/cli/src/detail/toolchain/pipeline.hpp` (+ pipeline.cpp) | new `interfaceHashForUnit(graph, unit, sema, surfaceMap)` and `exportedSurfaceFor(unit)` used by the patch classifier | ~80 lines |
| `src/cli/src/commands/clean/clean.cpp` | remove `.cp3/dev/` (one line, comments updated) | 3 lines |
| `benchmarks/cases/compute/dev-reload/README.md` | post-implementation dated measurement (never hand-edited; D67 scorecard step) | n/a |

Total: ~4,500 lines of new code + ~500 lines of additive edits, one
non-frozen implementation file (`eventloop.cpp`) with two call sites, zero
frozen-header edits.

### 13.1 Suggested order

1. **Slice 1 (1-2 days): classification only.** 7.1 (identify M, re-sema,
   interface + state hashes) riding on the existing plural pipeline, with the
   patch path DISABLED. Output: dev.cpp prints
   `patch candidate: body-only (hash stable)` / `interface changed`.
   Risk-free, immediately validates the classification on real edits. This is
   also the measure of whether the phase-3 incremental sema is good enough.
2. **Slice 2 (2-3 days): module libs at startup.** dev_build + bootstrap +
   verification, no swaps yet. `cp3c run` gets the new layout; releases
   untouched; G17 passes; measure G9 (expected to already improve the
   benchmark by removing the exe relink).
3. **Slice 3 (2-3 days): state block + descriptor.** codegen state_map +
   module_var dev path + `__cp3_state` threading + descriptor emission.
   Full corpus (G1/G2-lite with a manual in-process swap test harness via
   `runOnce()`) validates byte-identity before any watcher integration.
4. **Slice 4 (2 days): safe point + swap + retirement.** `hotpatch.cpp`,
   eventloop hook, registry rewrite in app, `.cp3/dev` handshake files,
   watcher thread, ack handshake. Unit tests for the registry/swap.
5. **Slice 5 (1-2 days): dev.cpp integration + fallbacks.** F1-F10 fixtures,
   refusal rendering, Phase 3 fallback verification (G5, G7, F7).
6. **Slice 6 (1 day): gates.** G8/G9 benchmark runs (D67 scorecard appended
   to the dev-reload README with a dated row), ASan job, G17 strings scan.
7. **Slice 7 (0.5 day): DECISIONS.md D82/D83 landing**, cli-tooling docs
   one-pager, `clean` docs.

### 13.2 The riskiest step, honestly ranked

1. **Slice 3 - the state-block + `__cp3_state` lowering (module_var.cpp +
   call lowering).** It touches the hottest, most assumption-laden part of
   codegen (how every module-scope binding and every function's signature is
   spelled), and a single mis-threaded parameter is a silent memory-layout
   bug that the corpus may or may not catch quickly. It is also where
   byte-identity can silently break (initializer order). Mitigation: Slice 1
   and 2 land first; Slice 3's corpus runs on BOTH the full test suite and a
   dedicated byte-identity fixture; the module-var dev path is a separate
   code path so release cannot regress.
2. **Slice 4 - the safe-point hook and the handshake.** Small code, but it
   runs on the event-loop thread under the D62 invariant; a blocking call
   inside `hotPatchSafePoint` (e.g. an unexpected synchronous file read that
   stalls) would delay microtasks between macrotasks and silently change
   observable timing (not ordering) for network-heavy apps. Mitigation: the
   hook performs no blocking IO when the watcher flag is clear; file IO is
   bounded (~1-2 small reads); unit-tested with a fake pending file; ASan
   job covers it.
3. **Slice 5 - the watcher/ack fallback.** If the wake channel (7.4 step 4)
   never fires, all patches time out into Phase 3 and the metric is missed
   while everything looks "correct". Mitigation: the no-op `postFromThread`
   wake is implemented first and unit-tested under a fully idle loop (a
   blocked `waitForNext()`), which is the exact case an empty server app
   exercises.
4. Symbol visibility / `--no-undefined` across the two toolchain
   configurations is a solved problem in design but WILL produce build
   breakage on at least one host; budget a day.

---

## 14. Explicit non-goals, risks, and what CANNOT be hot-patched

### 14.1 Non-goals

- Release binaries: no change of any kind (D56, and section 2).
- No new user flags, env vars as API, or config keys (env vars appear only as
  test seams; section 12).
- No interpreter, JIT, or tracing. The patch replaces opaque native code; it
  never re-executes a suspended frame.
- No cross-process debugging story (the app is not a debugger); DAP stays
  as-is.
- No partial-program patches spanning multiple modules in one step (a
  multi-module interface change rebuilds everything; a multi-module body
  change is also out - ONE module lib per patch in v1; batching is future
  work).
- No removal of the Phase 3 rebuild path. Hot patch is an acceleration of dev;
  the fallback IS the product's correctness floor.
- No attempt to make `cp3c run` (one-shot) avoid process startup; only the
  compile/link portion is attacked.
- No unload of old module libs before exit in v1 (7.7). DoD is bounded leak.

### 14.2 Hard exclusions from hot patch (all fall back to Phase 3)

| Excluded | Why | Detection |
| --- | --- | --- |
| Top-level `await` in any unit of the graph | suspended state machine frames capture generation-1 code pointers (8.5) | descriptor `flags & bit0` |
| Any program using `new Worker(...)` | workers execute module code on threads with no safe point; no uniform retirement possible without anchors | CLI checks graph for `WorkerSite`s before patching |
| Class, interface, enum, alias changes (any) | interface hash refuses (F2); class bodies are re-emitted per consumer namespace in this codebase, so no single-lib swap can fix them | interface hash |
| State layout changes | offsets would differ (F3); reusing the block would corrupt every field after the first inserted binding | state layout hash |
| Interface/ABI mismatch | wrong compiler/runtime generations (F4/F5) | ABI tag |
| Module reached through `cpp/` or `native` (capability ABI edges) | their symbols are not in our table; a swap could call stale native backends | unit flag from native-row selection; treated as interface |
| Prelude/`_prelude*.cp3` edits | prelude membership change = topology change (7.1 step 3); re-sema everywhere; always rebuild | content diff hits prelude path |
| `package.json` / graph topology | same | manifest diff |
| Patch arriving while the app has pending external work (libuv, servers) | allowed (it applies at the next safe point); only a 250 ms no-ack triggers restart - documented, not prevented | ack timeout (F7) |

### 14.3 Risks and open questions for the lead

1. `eventloop.cpp` is the ONLY existing implementation file modified, and it
   is owned by the runtime owner; the two-line hook needs their sign-off. If
   denied, the fallback is a documented `#if CP3_HOT_PATCH` in the same file
   or, worst case, the deferred design: drive patches from `setDrainHook`
   (loop-end only) which does NOT satisfy G8/G10 and should be treated as a
   stop-ship for this design, not as a compromise.
2. Debug profile (`--debug`) keeps the static single-TU build with #line
   fidelity; Incremental becomes "the dev build" with the shared-module
   layout. If the lead wants Debug to also gain hot patch, that is a trivial
   follow-up (same path, lineDirectives on) but doubles review surface; not
   proposed.
3. Module stem hashing (section 11, ASCII stems) means `registry.json`
   carries both the logical name and the stem; debugging ergonomics depend on
   the registry being printed with `--verbose`.
4. Anchor-based precise reclamation (14.1/7.7) is the designed-next step; if
   the lead wants v1 to unload old libs eagerly anyway, the cost is a rewrite
   of async capture lowering; recommend keeping v1's never-until-exit rule.
5. `.cp3/dev/registry.json` becomes a second cache manifest; whether it
   belongs under the D61 "intermediates only" reading is a lead call
   (recommended: yes, documented as D61-consistent dev intermediates).

---

## 15. The dev-loop latency budget

Target: ~100-300 ms from quiet (debounce window expired) to the app printing
with the new body, for a one-function body edit in a typical dev project
(one changed `.cp3`, PCH warm, clang warm).

| Segment | Budget | Basis / honest caveat |
| --- | --- | --- |
| Debounce (existing, unchanged) | 25 ms | `kDebounceWindow` dev.cpp:65; included so the budget is honest |
| Stat confirm + content fingerprint (existing) | ~1 ms | one file read per watched file |
| Graph load | ~5-15 ms | module graph build today; one `.cp3` parse, small |
| Frontend re-sema of ONE changed unit | ~20-60 ms | HARD DEPENDENCY: assumes the phase-3 incremental sema caches unchanged units and re-runs only M. If not landed yet, the whole-graph sema adds today's cost (~100-400 ms for small programs) and the target is missed; this is the single largest unproven item. Mitigation: Slice 1 measures it; if the cache is not ready, the patch path still works but lands in the 300-600 ms band, and G9 reports the real number |
| Interface hash + state layout hash (fnv1a64) | ~1-2 ms | hashing per unit only; provenance is `interface_hash.hpp` |
| Codegen of ONE unit (plural pipeline) | ~10-20 ms | today this cost exists and is invisible next to a full rebuild |
| Write unit TU + fsync directory | ~1-3 ms | |
| clang -O0 -c with PCH (one unit) | ~80-180 ms | PCH proves ~100-200 ms for runtime TUs at -O0 (`build.cpp` :438 comment); module TUs are comparable; the exe-class cold case (PCH miss) can hit 400 ms and is reported, not hidden |
| clang -shared (module lib link) | ~20-60 ms | one DLL, no LTO; lld-class linkers shine; MinGW bfd slower; unproven on Windows, must be measured |
| fsync lib + write patch.json + rename pending | ~2-5 ms | |
| App: watcher poll interval | 0-25 ms | polls every 25 ms; average adds ~12 ms |
| App: wake + safe point + verify + LoadLibrary + memcpy + atomic flip | ~5-20 ms | one small DLL map; dominated by `LoadLibraryW`; AV interference can add 20-200 ms on unpatched endpoints (section 11) |
| applied-<gen>.json ack seen by CLI | ~1-5 ms | 5 ms poll granularity |
| **Total (PCH warm, sema incremental)** | **~165-355 ms** | median estimate ~250 ms; meets "~100-300 ms" at the median, honestly may exceed 300 ms at p90 on a cold PCH or a big unit |

Compare against the current path end-to-end (~2.1 s client-side figure; the
2026-11-13 benchmark row: 2751.8 ms median, `dev-reload/README.md`): the
delta is (a) no whole-graph sema if incremental lands, (b) no runtime TU
recompiles (already cached today), (c) no exe relink (removed by the shared
layout - Slice 2 captures most of this for `cp3c run` today without any
swap at all), (d) no child restart, no process spawn, no runtime re-init.

Unproven, called out honestly: incremental per-module sema cost (this is the
second half of v0.2.5); module lib link time on MinGW bfd; AV overhead on
enterprise Windows; the ack round-trip under a fully idle app (works by
construction via `postFromThread`, but G8's fixture must be run on real
hardware, not extrapolation).

Also noted: the app applies the patch at the NEXT safe point, not at wall
clock; if the app is parked in a blocking libuv wait with no timer armed,
the 25 ms watcher thread still wakes it (section 7.4 step 4), so this is not
a failure mode - but the *sponsor* of the latency budget is the watcher poll
interval, not the event loop's idle behavior.

---

## 16. Decisions proposed (DECISIONS.md style)

> **D82 - Dev-only native hot patch.** Incremental builds only: a dev exe that
> is a thin bootstrap plus one shared lib per checked unit under `.cp3/dev/`,
> plus one dev shared `cp3runtime` for the whole process (D71's one EventLoop
> is preserved by construction). A body-only edit of a checked unit builds
> ONE module lib; the running app verifies the descriptor (ABI tag =
> (interface_hash, compiler_key, runtime ABI); state layout = (size, align,
> layout hash, per-field offsets)) and atomically flips the module's table to
> an immutable snapshot at the event-loop macrotask safe point. Mutable
> module-scope `let`/`var` storage lives in a host-owned state block that a
> code swap reuses; initializers stay in source order. Interface or layout
> changes, compile errors, ABI mismatches, and load failures all degrade to
> the Phase 3 rebuild+restart path (D62 preserved throughout). Old module
> libs stay mapped until process exit; v1 performs no early unmapping.
> Release, Debug and every non-Incremental artifact are unchanged (D56).

> **D83 - Hot-patch classification is mechanical, not discretionary.** A unit
> edit is body-only iff the per-unit interface hash (interface_hash.hpp
> builders over the ExportSurfaceMap) and the per-unit state layout hash are
> both bit-identical AND no unit in the graph has top-level await AND the
> graph contains no `new Worker` site AND the changed set is exactly one
> checked unit whose shared-header fingerprint is unchanged. Entry-module
> edits qualify like any other unit. Top-level statements of any unit run
> once at process start and are never re-executed by a patch.
> Everything else rebuilds and restarts. The app is the verifier of both
> hashes at the safe point; the CLI is a predictor; the two must agree or the
> swap is refused. Refusal never kills the running app.

Cross-references to bind: **D9** (number text; dev output byte-identical),
**D56** (release single binary; unchanged), **D61** (`.cp3/build|out|dev` are
all intermediates; `clean` removes `.cp3/dev/`), **D62** (macrotask boundary
hook at eventloop.cpp:398; ordering proof in this document, section 8),
**D64** (performance doctrine; G8/G9 enforce the ~100-300 ms budget with a
D67-style labeled run; the 2751.8 ms baseline is the before row),
**D68/D72** (std family TUs feed the one shared dev runtime unchanged),
**D71** (one event loop per thread; the wake channel rides `postFromThread`,
never a second loop), **D12/D14** (superseded toolchain pinning and the
frozen-interface rule; nothing frozen is reshaped by this design).

---

## 17. Appendix A - worked descriptor for the smallest real case

Module `src/counter.cp3`: `let counter = 0; export function bump() { counter += 1; return counter; } export function read() { return counter; }`

Emitted into `cp3_unit_2.cpp` (this is the descriptor section and the state
declarations, appended by the Incremental path):

```cpp
// ---- dev descriptor (Incremental only) ----
namespace cp3_mod_counter {
struct __cp3_state_layout {  // documentation view; offsets are literals below
  double counter;            // let counter = 0; is a cp3 `number`
};
}  // namespace cp3_mod_counter

extern "C" {
const std::uint64_t cp3_module_interface_hash_v1  = 0x8a1f...;       // computed
const std::uint64_t cp3_module_abi_compiler_key_v1= 0xb2c4...;       // computed
const std::uint32_t cp3_module_abi_runtime_v1     = 1u;              // constant
const std::uint64_t cp3_module_state_layout_hash_v1 = 0x5521...;     // computed
const cp3_state_field_v1 cp3_counter_fields[1] = {
  {"counter", "number", 0u, 8u, 8u},
};
const cp3_module_descriptor_v1 cp3_descriptor = {
  sizeof(cp3_module_descriptor_v1),  // struct_bytes
  1u,                                // struct_version
  1u,                                // abi_runtime
  0u,                                // flags (no top-level await, no workers)
  0x8a1f...,                         // interface_hash
  0xb2c4...,                         // compiler_key
  0x5521...,                         // state_layout_hash
  8u, 8u, 1u,                        // state_size, state_align, field_count
  cp3_counter_fields,                // state_fields
  2u,                                // table_count
  "src/counter",
  "cp3_mod_counter",
};
const cp3_module_descriptor_v1* cp3_get_module_descriptor(void) {
  return &cp3_descriptor;
}
bool cp3_module_fill_table_v1(void** out, std::uint32_t n) {
  if (n != 2u) return false;
  out[0] = reinterpret_cast<void*>(&cp3_mod_counter::bump);
  out[1] = reinterpret_cast<void*>(&cp3_mod_counter::read);
  return true;
}
void cp3_module_state_init_v1(void* block) {
  new (block) cp3_mod_counter::__cp3_state_layout{};  // value-initializes counter
}
void cp3_module_state_destroy_v1(void* block) {
  static_cast<cp3_mod_counter::__cp3_state_layout*>(block)->~__cp3_state_layout();
}
void cp3_module_init_v1(void* __cp3_state) { /* top-level statements */ }
}
```

Cross-module call after the change, from a consumer's TU:

```cpp
// import { bump } from "./counter.cp3";  console.log(bump());
const cp3::runtime::dev::TableSnapshot* t =
    cp3::runtime::dev::tableFor(/* moduleId counter */ 2u);
using Fn = double (*)(void*);
double v = reinterpret_cast<Fn>(t->entries[0])(t->state);
```

A body-only edit to `bump()` builds a new `counter.<n>.dll`; the app flips
slot 0's target; the state block is untouched; `counter` reads and writes
keep working with no re-initialization.

---

## 18. Appendix B - refusal and rollback of tiny deltas

Every irreversible operation in the protocol is either (a) behind the rename
commit point, or (b) process-local and discardable:

- `.cp3/dev/modules/.<stem>.<gen>.tmp.dll` is deleted on any CLI-side failure
  before `pending` is renamed; after publish, the lib never moves (the app
  holds its path in `registry.json`), and a refused generation is left
  unused on disk, never loaded again.
- `registry.json`: written to `registry.json.tmp` then renamed; the app only
  rewrites it AFTER its own bootstrap/patch verification succeeds, so a
  crash mid-rewrite leaves the previous file intact (and the next startup
  re-verifies everything against the libs on disk anyway).
- If generation N applied cleanly and generation N+1 is refused, the app is
  pinned at N; registry.json keeps N; the CLI learns the refusal from the
  ack file and falls back to Phase 3. There is no global "patch journal" to
  corrupt and no partial commit state to reconstruct.

---

## 19. Appendix C - file:line citation index (load-bearing facts)

- plurality pipeline, shared header, unit TU, entry TU: `src/codegen/src/emit/decl/program.cpp`
  :86, :98-133, :277-283, :300-332, :336-359, :361-378.
- `ProgramAssembly` / `ProgramUnitText` / `ProgramLink`: `src/codegen/src/detail/generator/program_assembly.h` :64-81;
  `program_link.h` :39-76.
- Module graph / `compilationOrder()`: `src/common/include/cp3/meta/module.hpp` :88-118.
- dev watcher (25 ms debounce, fingerprints, restart rule): `src/cli/src/commands/run/dev.cpp`
  :65-69, :178-241, :666-739.
- Incremental -O0 profile selection: `build.cpp` :103-128; PCH: :434-531;
  per-module jobs + shared-header fingerprint: :88-94, :539-582, :539-560;
  link-stamp helpers: :717-760 (saveManifest trailing).
- Layout (`.cp3/build`, `.cp3/out`, manifest): `src/cli/src/pkg/cache/cache.cpp` :33-51, :76-110;
  `src/cli/src/detail/cache/cache.hpp` (header comment, :8-34).
- Event loop macrotask boundary and ordering: `src/runtime/src/async/eventloop.cpp`
  :216-228 (drainMicrotasks), :230-273 (runOnce), :262-272 (comment +
  CycleCollector), :389-420 (run), :398-399 (drain then pumpReadyTasks),
  :437 (runEventLoop); `src/runtime/include/cp3/async/eventloop.hpp` :227-246,
  :306-331.
- Std console async pump (dev runtime contents): `src/std/console/runtime_pump*.cpp`; runtime TU selection
  markers: `src/cli/src/toolchain/runtime/util.cpp` :322-343.
- Interface hashing: `src/cli/src/detail/toolchain/interface_hash.hpp` (full file; :13-16 "NOT
  wired up yet", :86 interfaceHashOf, :116 bindingToInterfaceEntry);
  per-unit export surfaces: `src/cli/src/toolchain/runtime/pipeline.cpp` :37-46, :83-160, :296-360, :527-560;
  AST export surface: `src/modules/src/support/export_surface.cpp` :135-160.
- Runtime ABI ownership: `src/runtime/CMakeLists.txt` (cp3_runtime target list);
  frozen runtime headers: `src/runtime/include/cp3/**` (D14).
- Module-scope mutable let/var lowering (state block migration source):
  `src/codegen/src/emit/decl/module_var.cpp` (whole file; `isStaticInitExpr` :39).
- Codegen options (frozen): `src/codegen/include/cp3/codegen.hpp` :56-83.
- Incremental profile selected by pipeline: `src/cli/src/commands/run/run.cpp` :172-198.
- Benchmark baseline: `benchmarks/cases/compute/dev-reload/README.md` (2026-11-13 row: 2751.8 ms median).
- Single-binary guarantee (release): `DECISIONS.md` D56 :296; D61 artifacts :368;
  D62 rulings :397; D64 perf doctrine :433; D68 std tables :538; D71 one loop/thread
  :629; D72 std layout :686.
