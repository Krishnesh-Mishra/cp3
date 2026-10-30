---
title: Why CP3 exists
chapter: Welcome
---
# Why CP3 exists

CP3 explores a practical gap between a productive, approachable language and a native program. It keeps common JS/TS spelling—bindings, functions, classes, interfaces, generics, and async syntax—while making the compiler responsible for checking types and producing native code.

## The trade CP3 is trying to make

| Concern | JavaScript / TypeScript workflow | CP3 workflow |
| --- | --- | --- |
| Feedback | Runtime plus editor/type-checker tools | Compiler diagnostics during `check` and `build` |
| Deployment | Node or browser runtime is part of execution | Native executable produced through C++ |
| Memory | JavaScript engine manages a tracing heap | CP3 runtime manages references; no tracing GC |
| Low-level control | Usually behind native extensions | C++ generation and native SDK interop are available when needed |
| Learning curve | Familiar to JS/TS developers | Deliberately reuses much of that surface |

These are design goals, not a promise that every Node API, TypeScript type feature, or performance profile is already supported. CP3 has its own module set, diagnostics, and runtime contracts. Check the relevant pages before porting a package.

## Why compile through C++?

The compiler emits C++ as a portable native backend and uses a configured compiler to optimize and link the result. This lets CP3 reuse mature C++ toolchains while keeping C++ syntax out of the normal application workflow. The generated C++ is also useful when investigating a compiler issue or a performance bottleneck.

## Why no tracing garbage collector?

The runtime uses reference ownership for reference-shaped values and ordinary values for primitives. This avoids a tracing collector, but it is not the same ownership model as Rust and it does not automatically break strong reference cycles. See [The CP3 value model](#02-runtime/01-value-model) and [Reference ownership and cycles](#02-runtime/03-reference-ownership).

## Who this language is for

CP3 is aimed at application developers who want a statically checked language with approachable syntax and native deployment. It is not a claim that a JS-like syntax is always the best interface for systems programming, nor that CP3 already matches the ecosystem breadth of Node, Rust, Go, or C++.

The right way to evaluate CP3 is to build a real workload, inspect its generated code where needed, check the result, and measure it with a recorded benchmark run.

## Why not other Languages

CPP/Rust are one of the best Language ever used by me but there is a main problem which is COMPLEXITY, I dont like the manual memory management or borrow mechanisms of these languages, also the syntax is so complicated that reading them in large projects look like reading a calculus problem to find what happens when einstien fell into it with his kidneys did that strech or compressed. Just Joking.

Now comes Two of my favourate Languages PYTHON & JS these are one of my favourate because i am connected to them from my childhood. When i played Hide And Seek some of my frineds would definetly suddenly scared me these are those 2 friends who will disturb my Saturday night 2AM, with sudden bugs crashing server.  Also these are very slow which was the main reason to avoid them , thier syntax is easy but the ram cusumtion is also very bad.

Now GO , this is the language wwhich solves most of the things CP3 tries to solve, Easy Syntax (not as complicated as rust or CPP) , Native Executable And High Peroformance. This is one of the Exeptional Languages Created but with one problem GC. I dont like garbage collector because some time they are the ones which cause that 5ms delay in every few sec. if there was something which removes the gc from GOLANG then i wwould definetly not make this new language.
BTW thats not true i like TS Syntax, I would have made a JS -> Go translator and not juggled CPP files. which would have been easy, Because my personal favourate Language is TS. I like its Syntax.