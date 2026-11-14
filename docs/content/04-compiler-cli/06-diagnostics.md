---
title: Diagnostics and generated help
chapter: Chapter 4 · Compiler and tools
---
# Diagnostics and generated help

CP3 diagnostics are compiler feedback tied to the source program. Depending on the stage, a diagnostic can include a stable code, a message, a source span, notes, and suggested actions. The compiler can also generate a local HTML help page for a diagnostic and rank relevant guide sections by text similarity.

## Read the diagnostic before editing

Start with the earliest diagnostic in the output. Read its code, message, source location, and notes together. A note may point to a declaration that conflicts with the current use; the message alone may not explain the full context.

The source location identifies the CP3 file and span where the compiler detected a problem. It is more useful for a language error than a line number in generated C++. For a native compiler failure, preserve both locations: the CP3 source that led to the output and the generated C++ location.

When an error mentions a missing or unknown member, compare the exact member spelling with the module's guide page. CP3 standard modules have closed supported surfaces; a similarly named Node or browser API is not automatically available.

## Diagnostic codes

CP3 assigns stable codes to diagnostic classes. A code helps locate implementation and regression tests and is safer to cite than wording that may be clarified over time.

Use the code to:

- search the diagnostic definitions and reporting sites in the source tree;
- search test expectations for the error category;
- link an issue to a specific class of compiler behavior;
- compare diagnostics across compiler revisions.

The code does not by itself identify which source change will fix the program. Read the full message and inspect the surrounding declarations and types.

## Local per-error help page

When CP3 can create a help page, it writes an HTML file into the operating system's temporary directory and prints a local `file:` URL. Open that URL in a browser to see the diagnostic context and related documentation.

The page can contain:

- the diagnostic code and message;
- the source excerpt, location, and caret when source information is available;
- notes and suggestions supplied by the diagnostic;
- the three documentation sections with the strongest text-similarity scores.

The related sections are selected algorithmically from the diagnostic text and documentation text. CP3 tokenizes text, computes cosine similarity using term weights, and presents the highest-ranked matches. This is a retrieval aid: a score is not a probability that a page is correct, a semantic proof of relevance, or a hard-coded diagnostic-to-page association. Read the selected page and follow its links; use the compiler message as the authority for what the program must change.

If an error produces no page, the diagnostic remains complete in the terminal output. The help page is optional assistance and is not required for compilation.

## The full documentation server

Run:

```sh
cp3 docs
```

The compiler serves its embedded guide on loopback and prints the address. The default port is `4173`. To let the operating system choose an available port:

```sh
cp3 docs --port 0
```

The command stays active while the server is running. Stop it with the normal terminal interrupt when finished. It serves the bundled local guide and does not depend on a remote documentation website.

The docs command and per-error HTML page solve different tasks: one presents the complete guide; the other opens a compact, diagnostic-centered view with ranked guide matches.

## Separate CP3 errors from native failures

| Output clue | Stage | What it usually means |
| --- | --- | --- |
| `CP3xxxx` code and `.cp3` source span | Lexing, parsing, module resolution, or semantic analysis | CP3 rejected the source or an imported interface. |
| Generated `.cpp` path and C++ diagnostic | Native compilation | Code generation, runtime headers, toolchain compatibility, or a compiler issue needs investigation. |
| Undefined symbol or missing library | Linking | A runtime/native object or dependency is absent or misconfigured. |
| Stack trace, thrown value, or process exit after launch | Execution | The built program encountered a runtime condition. |

`cp3 check` runs the front end and does not prove that C++ generation, native compilation, linking, or program execution will succeed. A successful check is one useful gate, not a complete build.

## Report a useful compiler issue

Include a small source reproducer, the exact compiler version, the full diagnostic, operating system, selected type system, and the command you ran. State whether failure occurs during check, generated C++ compilation, linking, or execution.

For code generation or performance issues, also include:

1. the generated C++ from the same build (`--show-runtime` can preserve runtime artifacts);
2. the selected native toolchain and version;
3. the build profile and flags;
4. benchmark repetitions, median, variation, and output verification;
5. relevant compiler optimization remarks, with the file and line tied to the source loop.

Avoid submitting only a timing or a screenshot if the command output and source can be shared. A concise reproducer makes it possible to distinguish a language rule, compiler regression, runtime issue, and environment problem.

## Related pages

- [Compilation pipeline](./02-compilation-pipeline.md)
- [Generated C++ inspection](./10-generated-cpp-inspection.md)
- [Tests and compiler verification](./08-tests-and-verification.md)
- [The standard library](../03-standard-library/00-overview.md)
