# Rule 3 — package types are independent of the consumer's `typeSystem`

Status: implemented in `stripToSignatures`
(`src/cli/src/pkg/manifest/signatures.cpp`); called by `packProject` /
`collectEntries` (`src/cli/src/commands/pkg/pack.cpp`). No new diagnostic
code.

## 1. Problem

A `.cp3pak` ships one declaration surface, `index.cp3`, generated from the
package's entry module. A consumer's front end reads that file to type-check
imports — under the **consumer project's** `typeSystem`
(`rust` / `cpp` / `typescript`). A bare native spelling is only a native
type under the vocabulary that was current when it was written:

```ts
export function offset(dividend: i64): i64 { ... }   // rust package
```

Under a `cpp` consumer, `i64` is just an unknown ordinary name; under a
`typescript` consumer it is an ordinary user name. The same package would
therefore export *different types to different consumers* — its surface
would change with the consumer's `typeSystem`. That breaks package interop
(Rule 3): an exported type must mean the same thing in every project.

## 2. Decision: prefix at generation time, keyed by the author's `typeSystem`

`index.cp3` generation takes the author's `typeSystem` (from the package
build's `CompileOptions::typeSystem`, i.e. the package's own
`package.json`) and rewrites every unprefixed native spelling it copies
into the prefixed form:

| Author `typeSystem` | `i64` becomes | `long long` becomes |
| --- | --- | --- |
| `rust` | `r_'i64'` | — (not a rust spelling) |
| `cpp` | — | `c_'long long'` |
| `typescript` | verbatim (no rewriting) | verbatim |

Prefixed spellings (`r_'…'`, `c_'…'`, `ts_'…'`) resolve identically in
every project (`resolveNativeTypeName`), so the generated surface denotes
the same native types no matter which `typeSystem` the consumer uses.
The rewrite is driven by the *authoring* `typeSystem` only, never the
consumer's: the authoring value is the only one that knows whether a bare
`i64` was meant as a native type, and the prefixed form needs no
consumer cooperation.

Only the spelling is prefixed; the quoted text is the author's own
(`named.name`), so an alias keeps the exact identity it had in the
authoring project (`long` stays `c_'long'`, it is not canonicalized to
`long long`).

### Why not normalize to a canonical id instead?

Prefixing the author's spelling is a purely syntactic rewrite that
preserves the author's type identity exactly. Canonicalizing would need a
spelling-normalization table in the CLI and could silently change the
type a package exports (e.g. an author's `long`, which is platform-width,
vs a canonical 64-bit spelling). It also would need `ts_` spellings for
TypeScript-authored packages, whose ordinary names must *not* be
reinterpreted as natives — so TypeScript packages copy verbatim instead
(they have no native spellings to rewrite).

## 3. Mechanism (`signatures.cpp`)

`stripToSignatures(file, program, typeSystem)` walks the parsed module
*before* copying any text, recording a span-keyed rename for every
`NamedType` whose name resolves as a plain native type under the authoring
`typeSystem` (`resolvePlainNativeTypeName`). Declarations are then copied
source-span-first as before, with the renames spliced into the copied
slices (`Normalizer::slice`). Because renames are keyed on source offsets
and only applied inside copied slices, everything else (parameter names,
formatting, comments inside heads, bodies' elision) is untouched.

Coverage of type-bearing positions:

- **Type nodes** (recursive): `NamedType` + type arguments, unions,
  arrays, tuples, function types, object types, parenthesized types;
  generic constraints/defaults; interface `extends`; class `extends` /
  `implements`; signature members.
- **Declarations**: function/class/interface/type-alias/variable
  declarations, class methods/constructors/properties/accessors,
  exported forms of all of the above.
- **Expressions copied verbatim** (extended walker): initializers
  (variable, property, enum member), `export default <expr>`, explicit
  generic arguments on calls/news (`f<i64>(...)`, `new Box<i64>()`),
  `as` casts, arrow-function parameter/return annotations, decorator
  arguments, computed keys, `extends` expressions, and destructuring
  default values. A generic statement walker covers bodies that are
  copied verbatim (arrow bodies inside exported initializers).
- **Static blocks** are runtime, not surface: their bodies are elided
  (`static {}`) rather than copied, so they cannot leak annotations.

`PrefixedType` and `LiteralType` nodes are already neutral and are never
rewritten. TypeScript-authored packages (`typeSystem ==
TypeSystem::TypeScript`) skip the walk entirely and copy verbatim.

## 4. Edge cases

| Case | Behaviour |
| --- | --- |
| Ordinary type named like a native in another vocabulary (`long` in a rust package) | Resolves only under the author's vocabulary; copied as written. |
| Multi-word / folded spellings (`unsigned long`, `long long`) | Parser folds them into one `NamedType`; renamed only if the source bytes match `named.name` exactly, otherwise left verbatim (conservative). |
| Already-prefixed native (`r_'u32'`) | `PrefixedType`; never rewritten. |
| Native spelling in a non-exported, dropped statement | Rename recorded but never spliced (its span is not copied); harmless. |
| Duplicate visit of the same node | Deduped in `note()`; splice cursor is monotonic, so a rename can never be emitted twice. |
| `export { a }` / `export * from "…"` | No type spellings; copied verbatim. |
| Static block | Body elided; only `static {}` ships. |
| Consumer is `typescript` | Prefixed forms still resolve (`r_'…'` / `c_'…'` are valid in every project). |

## 5. Wiring

- `stripToSignatures` already receives the authoring `typeSystem` from
  both call sites in `pack.cpp` (`packProject`, `collectEntries`);
  `ensureNativeImports` then adds the native imports the prefixed surface
  needs. No signature or call-site change was required for Rule 3.
- Fixture `tests/cli/fixtures/package-rust-native/` +
  case `publish-rust-native-signatures` (`tests/cli/cases/pkg/package.json`)
  assert `r_'i64'` in the packed `index.cp3`.

## 6. Limitations / follow-ups

- The walker covers the current AST's type-bearing fields. A future node
  kind that carries a `TypePtr` (or a copied-verbatim expression) must add
  a case here or its spellings will ship unprefixed. The `default: return`
  arms are deliberately silent, matching the file's conservative-copy
  design — they are the place to re-audit when the AST grows.
- Method-signature / function-type parameters are assumed to carry
  identifier names (as in TS declaration syntax); destructuring patterns
  there would not be walked. Declaration-site parameters *are* walked.
- Decorator *names* and computed keys are collected for annotations but
  are not themselves part of the type surface; a decorator spelled with a
  native-looking identifier is unaffected (identifiers are not types).
- This guarantees the *generated* `index.cp3` is consumer-independent. A
  hand-written `index.cp3` in a package (outside the generated path)
  still needs the author to write prefixed spellings.
