# Status

**Beta One.** The v1 language spec is considered locked for implementation - every core semantic decision below is final unless explicitly marked otherwise. Build against this document directly.

A small number of items were marked **"confirm before building this part"** inline where a real design call was made to unblock implementation but hadn't been explicitly signed off by the project lead - everything else is not up for reinterpretation. **As of the beta-one sign-off, none remain:** every previously-flagged item has been decided and recorded in [§23](23-open-design-questions.md)'s *"Resolved for beta one"* list, so this document is implementation-ready. If something in here seems ambiguous or under-specified in a way that isn't already flagged, stop and ask rather than assuming - do not silently pick a default for anything not already decided in this document.

**This document is the language spec only** - syntax, types, memory model, concurrency, classes, and everything else you write in a `.cp3` file. It has no CLI commands, package format, or compiler-pipeline detail in it. For the `cp3` tool itself (`run`/`check`/`dev`/`fmt`, the package manager, `.cp3pak`, native-library bindgen, diagnostics/crash-reporting internals), see the companion [CLI-README.md](../cli-tooling/00-overview.md). For exact standard-library function signatures, see [STDLIB-README.md](../standard-library/00-overview.md).

---

