---
title: Native packages and ABI boundaries
---
# Native packages and ABI boundaries

CP3 source types do not tame a foreign pointer, a C++ class, a Rust reference, or arbitrary native declarations. When you cross the FFI line, you are responsible for the ABI, the ownership, and the error behavior — exactly as if you were writing a thin C shim, because you are.

## Native scalar types are not general FFI

CP3 supports a closed set of native scalar annotations (`typeSystem` selects the vocabulary: `typescript`, `cpp`, or `rust`), and explicit prefixes can select vocabulary at a type site. These describe fixed-width integers and floats carried in CP3 values; they do not declare foreign functions or native objects. Pointer/reference spellings are rejected by the scalar resolver — a `c_...` annotation is not an instruction to import a C++ type. See [Project type-system selection](../language/07-native-types/01-project-type-system.md).

## Native SDK packages

`cp3 native-install <sdk>` generates a CP3-facing package from a native SDK's headers, using libclang (it loads the libclang from your installed LLVM at runtime — nothing is vendored for this). The result is a `.cp3pak`-style binding with a generated `index.cp3` surface and `native.json` metadata under `.cp3/native/<id>/`; it is installed into consuming projects as `.cp3/native/<id>/` too.

Generated bindings still need review: clang can parse the headers, but only the library defines the contract that matters — calling convention, ownership, error behavior, thread safety.

```sh
cp3 native-install /path/to/sdk --name my-sdk
cp3 native-install --help
```

The SDK headers must be available to the toolchain. Symbol linkage, calling conventions, and pointer ownership are yours to verify; a compiling binding is not automatically a correct one.

## Using a bound SDK from CP3

An installed binding becomes an importable module. Keep native handles behind a small wrapper module, and use the reactor (`std/io`) for readiness-based waiting instead of blocking the loop:

```ts
import io from "std/io";
import native "my-sdk" as sdk;

export async function waitForSocket(handle: number): Promise<undefined> {
  await io.pollUntilDone(
    () => sdk.socket(handle),     // re-read every iteration: connect can change the fd
    () => sdk.progress(handle),   // io.POLL_READING | io.POLL_WRITING | io.POLL_DONE | io.POLL_FAILED
  );
}
```

Blocking native calls still block the loop — wrap them in `async` only if they actually suspend for I/O. A library with no pollable descriptor is not async-ready: call it synchronously from a worker or accept the freeze, and document it. There is no `import native async "..."` syntax yet, and no offload pool — do not write it.

For distribution, a `.cp3pak` carries the binding metadata and artifacts; consumers get them at install time. Do not hand-edit a copied `native.json` in a consuming project.

## The contract to write down

For every exposed operation, document — then test:

- the exact symbol and calling convention;
- the native types and their CP3 mapping;
- who creates and destroys each resource;
- whether pointers can be null;
- whether buffers are copied or borrowed, and for how long;
- thread-safety requirements;
- how native failures become CP3 errors or result values;
- the required headers, libraries, and target flags.

Until those are written down, the binding is not a safe interface, no matter how clean the declaration looks.

## Ownership and lifetime

CP3's reference counting does not extend into foreign territory. A native handle can outlive or die before any CP3 value:

- Choose one ownership contract: the wrapper owns and destroys, the caller owns and must keep alive, or a documented retain/release pair controls it — and apply it consistently.
- A foreign pointer held in a CP3 object is opaque to the checker. Store a handle/identifier, not an address you expect CP3 to keep alive for you.
- Rust types like `String`/`Vec<T>` are not C ABI. Interop with Rust means an explicit `extern "C"` surface with plain C-compatible values or opaque handles, matched by the CP3 declarations.

## Errors and unwinding

Do not let a C++ exception or a Rust panic cross the boundary unless the contract is designed for it. Convert failures on the raising side into a documented status, or into a CP3 error value on the consuming side. For callbacks, state which thread invokes them and whether they can fire after the initiating call returns.

## Layout and target assumptions

Struct layout varies with compiler, target, packing, and ABI. Never map a native struct to a CP3 record by field name; agree on an explicit C-compatible layout, and test size/alignment/field order per supported target. Fixed-width integers only; convert to `number` only after range-checking, because `number` cannot exactly represent every 64-bit integer.

## Link troubleshooting

`cp3 check` passing proves nothing about linking. When a build fails at the native stage, inspect: the full compile/link command (`--verbose`), the library search paths, dependency order, target architecture, runtime dependencies, and symbol versions. A binding can be target-specific — test every OS/architecture you claim to support, and record the CP3, libclang, SDK, and library versions with the binding.

## Related pages

- [Native toolchains](./05-native-toolchains.md)
- [Project settings and build cache](./09-project-settings-and-cache.md)
- [Native scalar type limitations](../language/07-native-types/04-limitations.md)
- [Thread safety and ownership boundaries](../internal-engineering/04-async-non-blocking-io.md)
