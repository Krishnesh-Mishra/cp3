# Status

**Beta One.** Every module below now has a nailed-down function surface — not just shape/examples. Build against the signatures in this document directly. **As of the beta-one sign-off there is no open item left at the bottom** — the last one (`Buffer` encodings) was decided — so the standard library is implementation-ready.

**A note on notation:** every module below (`console`, `Math`, `fs`, etc.) is shown as `const moduleName: { ...methods... }` — this is describing the *shape of the plain object* that `import x from "std/x"` hands you, exactly like real JS's `console`/`Math`/`JSON` are just objects with methods on them. It is documentation notation only, not new cp3 syntax — cp3 has no `namespace` keyword and this doc doesn't introduce one. Where a signature line still says e.g. `randomUUID(): string;` inside one of these blocks, read it the same way you'd read a method on any other object type.

