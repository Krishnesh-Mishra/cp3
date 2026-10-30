---
title: Diagnostics and generated help
chapter: Chapter 4 · Compiler and tools
---
# Diagnostics and generated help

CP3 diagnostics include a stable code, a message, and source location when available. Some diagnostics include notes and suggested next steps. Fix the earliest relevant source error before investigating errors caused by generated C++.

## Reading a diagnostic

Start with the CP3 code and the source span. Read the full message, any notes, and suggestions together: a note may identify the declaration that conflicts with the current use. If the diagnostic points to an imported module member, check the corresponding standard-library page and the installed compiler's exact API surface.

## Local error help page

When the compiler can create a help page, it writes an HTML file in the operating system's temporary directory and prints a local `file:` URL. The page contains the diagnostic code and message, source line and caret when available, notes, suggestions, and the three guide sections ranked highest by TF-IDF cosine similarity.

This matching is a text-ranking aid. The scores measure term overlap; they are not probabilities, semantic proofs, or fixed diagnostic-to-page mappings. Inspect the selected sections and use the source diagnostic as the authority.

## Full guide server

Run `cp3 docs` to serve the full embedded Markdown guide and viewer on loopback. The default port is 4173; `cp3 docs --port 0` asks the operating system to choose a free port. The command prints the selected URL and stays active until interrupted.

## Reporting a compiler issue

Include a minimal CP3 source example, the CP3 version, the exact diagnostic, operating system, and whether the issue occurs during parsing, type checking, code generation, native compilation, or linking. For performance issues, include repeated benchmark statistics and generated C++ for the hot path.
