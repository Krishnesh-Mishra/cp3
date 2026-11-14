---
title: Diagnostics and generated help
---
# Diagnostics and generated help

A CP3 diagnostic is compiler feedback tied to your source: a stable code, a message, a source span, sometimes notes and suggestions. The compiler can also generate a local HTML help page for a diagnostic, linking the guide sections judged most relevant.

## Read the diagnostic before editing

Start with the **earliest** diagnostic in the output. Read the code, message, location, and notes together — a note may name the declaration that conflicts with the current use, which the message alone cannot. The location is a CP3 file and span, which is more useful than any generated-C++ line. If an error names a missing or unknown member, compare its spelling against the module's guide page first: CP3 standard modules have closed supported surfaces, and a similarly named Node or browser API is not automatically available.

## Diagnostic codes

Codes like `CP3401` or `CP3906` are stable class labels for diagnostic categories. Use them to search the compiler and the test suite, to link an issue, or to compare behavior across revisions — but a code does not tell you *what to change*; the message and surrounding types do.

## The local per-error help page

When CP3 can create a help page, it writes an HTML file into the system temporary directory and prints a `file:` URL. Open it in a browser to see the code, the message, the source excerpt (with caret when available), the notes and suggestions, and the guide sections the compiler judged most relevant to this diagnostic.

Treat the page as a reading aid: the "relevance" ranking is approximate, and the compiler message remains the authority on what the program must change. If no page is produced, the terminal output is complete by itself — the help page is optional assistance.

## The full documentation server

```sh
cp3 docs
```

Serves the guide embedded in the compiler on loopback (default port `4173`; `--port 0` asks the OS for a free port). Stop it with the normal interrupt. It does not depend on a remote website.

## CP3 errors vs native failures

| Output clue | Stage | What it means |
| --- | --- | --- |
| `CP3xxxx` code and span | Lex / parse / modules / types | CP3 rejected the source or an imported interface. |
| Generated `.cpp` and C++ diagnostic | Native compile | Compiler/runtime/toolchain issue, or a code-generator bug — report it. |
| `undefined symbol` / missing library | Link | A native dependency is missing or misconfigured. |
| Stack trace or thrown value after launch | Execution | The built program failed at runtime. |

`cp3 check` is the front end only: a green check proves nothing about native compilation, linking, or running — but it is a fast first gate.

## Reporting a good issue

Include: a minimal source reproducer, the exact compiler version (`cp3 version`), the full diagnostic, the OS, the selected `typeSystem`, and the command you ran. State the failing stage (check, native compile, link, run). For code-generation or performance issues, also include the generated C++ from the same build (`--show-runtime` preserves the runtime sources used), the toolchain and version, the profile and flags, and the measurement details. A timing or screenshot alone is never enough.

## Related pages

- [CLI commands](./01-cli-commands.md)
- [Generated C++ inspection](./10-generated-cpp-inspection.md)
- [Tests and verification](./08-tests-and-verification.md)
