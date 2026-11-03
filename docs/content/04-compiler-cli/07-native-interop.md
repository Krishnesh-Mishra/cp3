---
title: Native packages and ABI boundaries
chapter: Chapter 4 · Compiler and tools
---
# Native packages and ABI boundaries

Native integration is an explicit boundary. CP3 source types do not turn a foreign pointer, C++ class, Rust reference, or arbitrary native declaration into a safe CP3 value. A native integration must describe the ABI, linking, lifetime, and error behavior that the program actually uses.

## Native scalar types are not general FFI declarations

CP3 supports a closed set of native scalar types in annotations. `typeSystem` selects the vocabulary for supported unprefixed names, and explicit prefixes can select a vocabulary at a type site. These forms map value types such as fixed-width integers and floating-point values; they do not declare arbitrary foreign functions or native objects.

Pointer and reference spellings are deliberately rejected by the scalar type resolver. A `c_...` or `r_...` annotation should not be interpreted as “import this C++/Rust type.” See [Project type-system selection](../01-language/07-native-types/01-project-type-system.md), [Prefixed type names](../01-language/07-native-types/02-prefixed-types.md), and [Native type limitations](../01-language/07-native-types/04-limitations.md).

## Native SDK packages

The CLI includes `native-install` tooling for generating CP3-facing bindings from a native SDK using Clang's parsing support. It can inspect headers and produce package files, but generated declarations still need review against the library's actual ABI and ownership rules.

Use the installed command's help to see accepted SDK paths, package names, and options:

```sh
cp3 native-install --help
```

The SDK and its headers must be available to the toolchain. Installation can also depend on libclang and platform-specific libraries. Generated bindings and build metadata are distinct from the native SDK itself; distribution and runtime loading rules depend on the package and target platform.

## Keep the boundary small

Wrap native calls behind a small CP3 module. Convert input into CP3-owned values close to the call site, check return codes, and keep raw handles out of general application logic.

For every exposed operation, document:

- the exact symbol and calling convention;
- the native types and their CP3 mapping;
- who creates and destroys each resource;
- whether a pointer can be null;
- whether data is borrowed or copied;
- thread-safety requirements;
- how native errors become CP3 errors or result values;
- required headers, libraries, and target flags.

If these points are unknown, the binding is not yet a safe interface even when a declaration compiles.

## Ownership and lifetime

CP3's `Ref<T>` runtime ownership does not automatically own an allocation created by a foreign library. A pointer returned by native code can become invalid independently of CP3 reference counting. Conversely, freeing a native handle while CP3 still stores it can create a dangling reference.

Choose one clear ownership contract: the wrapper owns and destroys the handle, the caller owns it and must keep it alive, or a documented retain/release pair controls it. Do not mix these conventions implicitly. For buffers, specify whether the native function copies the bytes or borrows them only during the call.

Rust values such as `String`, `Vec<T>`, and `&T` are not C ABI types. Rust libraries intended for interop should export an explicit stable ABI, usually with `extern "C"` functions and opaque handles or plain C-compatible values. The CP3-side declarations and conversion code must match that ABI.

## Errors and unwinding

Do not allow a C++ exception or Rust panic to cross an ABI boundary unless the contract explicitly supports it. Catch or convert failures on the side that raises them, then return a documented status or translate it into a CP3 error. Likewise, CP3 exceptions should not escape through a foreign caller unless the ABI is designed for that behavior.

For a long-running operation, define cancellation and partial-output behavior. For callback APIs, explain which thread invokes the callback and whether the callback can run after the initiating call returns.

## Layout, alignment, and platform assumptions

Native structure layout can vary with compiler, target, packing, and ABI. Do not map a C++ class or Rust struct to a CP3 object by matching field names. Use explicit C-compatible layouts only when size, alignment, field order, and ownership are controlled and tested for each supported target.

Native integer widths can also be platform-dependent. Prefer fixed-width types at boundaries where the protocol requires fixed widths. Check signedness and range before converting to `number`, because general CP3 numbers use floating-point representation and cannot exactly represent every large integer.

## Build and link troubleshooting

Keep CP3 type-checking errors separate from native compiler errors. A front-end accepted declaration does not prove that the symbol is exported, a library is linked, or the ABI is correct.

When linking fails, inspect the complete native command, library search directories, dependency order, architecture, and runtime dependencies. When a native call builds but fails at runtime, verify symbol version, calling convention, data layout, handle lifetime, and error conversion.

Native packages can be target-specific. Test each operating system and architecture that you claim to support. Record the CP3 version, Clang/libclang version, SDK version, and library version with the binding source.

## Related pages

- [Native toolchains](05-native-toolchains.md)
- [Project settings and cache](09-project-settings-and-cache.md)
- [Native scalar type limitations](../01-language/07-native-types/04-limitations.md)
- [Thread safety](../02-runtime/10-thread-safety.md)
