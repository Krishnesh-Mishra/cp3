---
title: Native interop
chapter: Chapter 4 · Compiler and tools
---
# Native interop

CP3 includes explicit native type forms for C++ and Rust interoperability. They are intentionally separate from ordinary CP3 types: a native pointer or ABI type carries platform and lifetime assumptions that do not apply to `number`, `string`, arrays, or CP3 classes.

## Keep the boundary narrow

Put native declarations behind a small module boundary. Convert inputs into ordinary CP3 values as soon as practical, and validate lengths, nullability, ownership, and error results at that boundary. Avoid exposing native handles throughout application code.

## C++ interop

C++ interop declarations describe native symbols and types that the compiler can bind or generate calls for. The C++ declaration, calling convention, linkage, and build configuration must match the library being linked. A declaration that type-checks in CP3 cannot guarantee that the external symbol exists or has the declared ABI; the linker and runtime remain relevant.

## Rust interop

Rust interop requires an exported ABI surface that matches the CP3 native declaration. Rust ownership types such as `String`, `Vec<T>`, and references are not automatically equivalent to C++ or CP3 values. Use a stable C ABI and explicit conversion and ownership rules when crossing the boundary.

## Ownership and errors

Document who allocates and frees each native resource. Never let an owning native pointer outlive the resource it refers to, and do not assume CP3 reference counting owns an arbitrary foreign allocation. Translate native errors into a CP3 error or result shape at the boundary rather than allowing undocumented exceptions to cross an ABI.

## Portability and safety

Native declarations can depend on platform headers, compiler version, architecture, and linked libraries. Build and run an interop check on each supported target. Treat native pointers, casts, and manually declared layouts as unsafe: a wrong size, alignment, or lifetime can corrupt memory even when the CP3 source type checks.

## Tooling

Use `cp3 native-install` and the native toolchain commands documented by the installed CLI to configure packages and compiler support. See [Native toolchains](#04-compiler-cli/05-native-toolchains) for the distinction between building CP3 itself and linking a CP3 application.
