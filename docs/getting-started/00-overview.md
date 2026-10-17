# From TypeScript to C+++ in two days

You already know TypeScript. C+++ (`.cp3` files) is built so that a competent JS/TS developer is productive in **about two days** - the syntax is ~99% JS/TS, the type vocabulary is TypeScript's by default, and you never touch pointers, `new`/`delete`, header files, CMake, or C++ templates as you know them.

This guide deliberately **skips the internals**. It will not explain how memory is managed, how the compiler emits C++, or what a coroutine frame is - none of that is needed to write cp3. What you actually need on day one is:

1. **What stays the same** as TypeScript - so you don't re-learn it.
2. **What changes** - the handful of TS things that don't compile here, and what to use instead.
3. **What's extra** - the small set of things cp3 adds on top.
4. **The standard library and CLI** you'll use every day.

> C+++ is in **beta one** - the spec is the source of truth. This guide is a fast on-ramp to it, not a replacement for it. Every "read this next" link points into the full spec when you want depth.

## The two-day plan

| | **Day 1 - the language** | **Day 2 - stdlib & tooling** |
| --- | --- | --- |
| **Morning** | Get a project running; the mental model; what's identical to TS; what won't compile and why | The std library you'll actually use: `console`, `JSON`, `Math`, `fs`, `path`, `Date`, `process`, collections |
| **Afternoon** | What's new (unions, enums, structural interfaces, `unknown`); the extra keywords; an afternoon of reading one file end-to-end | Networking in one import; the `cp3` CLI commands you'll type; build and ship a small CLI tool |

## How to read this guide

- Read **[Day 1 - the language](01-day-1-the-language.md)** first, then **[Day 2 - the std library & the CLI](02-day-2-stdlib-and-cli.md)**.
- Keep the **[TypeScript → C+++ cheat sheet](03-cheatsheet.md)** open in a second tab.
- Every section ends with **why you'd open the full spec page** - that's your pointer for depth. You don't need any of it to start writing code.

<details>
<summary>The one-paragraph summary, if you read nothing else</summary>

cp3 is TypeScript you compile to a native binary. Same `let`/`const`, same classes and interfaces, same `async`/`await`, same array and string methods. Two things to internalize: **there is no `any`** (use `unknown`), and **plain objects have a fixed shape** (use `Map` for anything dictionary-like). Beyond that it's a handful of small rules - read on.
</details>

## What this guide assumes

- You know JS/TS: variables, functions, classes, promises, modules, `Map`/`Set`, and a bit of `tsc`.
- You have the `cp3` CLI available (see [Day 1](01-day-1-the-language.md) for `cp3 init`). If you don't yet, you can still read everything here - it's all language-level.
