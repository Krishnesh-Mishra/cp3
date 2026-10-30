---
title: Welcome to CP3
chapter: Welcome
---
# Welcome to CP3

CP3 is a statically typed language with a JavaScript and TypeScript shaped surface. It compiles source files into C++ and then asks a native toolchain to produce the executable you run.

The project is for developers who like the fast feedback and familiar syntax of JS/TS, but want a compiled native program and explicit compile-time checks. CP3 is its own language. It does not run Node, browsers, or arbitrary npm packages behind the scenes.

## Start with the guide that fits you

- **New to CP3, experienced with JS/TS:** read [For JavaScript and TypeScript developers](#00-welcome/03-javascript-typescript-developers).
- **Want to understand why the project exists:** read [Why CP3](#00-welcome/01-why-cp3).
- **Want benchmark context:** read [Performance, measured carefully](#00-welcome/02-performance).
- **Ready to create a project:** read [Create and run your first project](#00-welcome/04-first-project).
- **Want exact language rules:** start at [Chapter 1 · Bindings and declarations](#01-language/01-variables-and-bindings/01-bindings).

## What CP3 does

The compiler reads `.cp3` source, resolves modules, parses and checks types, generates C++, and compiles that output. The CLI owns the build directory, cache, toolchain selection, and diagnostics. A project does not need to contain generated C++ to use the normal `cp3 run` workflow.

Reference values are managed by the CP3 runtime. The language does not expose C++ pointers or require manual `delete`. That convenience does not mean CP3 uses a tracing garbage collector: the runtime’s ownership and lifetime rules are explained in [Values and references](#02-runtime/01-value-model) and [Escape analysis in the current compiler](#02-runtime/02-escape-analysis).

## Read the language guide in order

Chapter 1 starts with variable bindings, types, mutability, and scope. Later pages cover functions, control flow, objects, classes, interfaces, generics, modules, and errors. Chapters 2 and 3 describe runtime behavior and the standard library. Chapter 4 describes compiler and CLI workflows.

Examples and API listings are intended to match the compiler and runtime in this checkout. Where the implementation has a deliberate limit, the page says so instead of implying full JavaScript or Node compatibility.


