# `Record<string, V>` — index signatures (sema design)

Status: sema type-resolution landed (minimal patch); closed index
read/write enforcement + codegen lowering are follow-ups for the
member/codegen owners (exact lines in §6). No new diagnostic code.

## 1. Problem

CP3 banned index signatures: a computed object-type member
(`{[key: string]: V}`) is CP3803 (`kIndexSignatureBanned`,
`type_check.cpp:addPropertySignature`) and the parser rejects the same
spelling in interface/object-type position with CP3201
(`parser/src/decl/members.cpp:288`). The supported dynamic dictionary
was only `Map<K, V>` (`new Map<string, number>()`). TypeScript's
`Record<string, V>` therefore failed twice: no builtin name (CP3302,
undefined type) and no index-signature model to land on.

## 2. Decision: canonicalize to `Map<string, V>`

`Record<string, V>` resolves in `Analyzer::resolveNamedType`
(`src/sema/src/checks/type/type_check.cpp`, after the user
alias/interface/`typeNames_` lookups) to:

```cpp
Type record(TypeKind::Class, "Map");
record.parts = { Type::string(), args[1] };   // V is resolved eagerly
```

i.e. the identical `Type` that `new Map<string, V>()` yields
(`inferCollectionsConstruct`, `src/std/collections/sema.cpp:144`).
Rejected alternative: a distinct nominal `Class "Record"` with
`parts == {string, V}`. It preserves the spelling for codegen, but
with only the files in scope (`type_check.cpp`, `std_check.cpp`,
diagnostic codes) it would be *open*: `memberOfProbe` returns
`ambient()` for any unknown `Class` (member_check.cpp:231) and
`indexOfProbe` likewise (member_check.cpp:475) — arbitrary properties
allowed, index reads `ambient`, CP3803-style closure unachievable
without editing `member_check.cpp` / `std/collections/sema.cpp` /
`scopes.h` (`isCollectionsClass`). The alias gets closure today for
free and is a one-line re-canonicalization later if a distinct type
is ever wanted.

## 3. Semantics (v1)

| Form | Result |
|---|---|
| `Record<string, V>` | `Map<string, V>` (nominal, invariant) |
| `Record<"lit", V>` | same (string literal key accepted via `isStringish`) |
| `Record<K, V>`, `K` a type parameter | canonicalized, constraint deferred |
| `Record<number, V>`, `Record<boolean, V>`, … | CP3803 (§5) |
| `Record<V>`, `Record<>`, `Record<string>` | CP3418 (§5) |
| plain `{[key: string]: V}` (object/interface) | unchanged: CP3201 (parser) / CP3803 (sema) — out of scope |
| `type R<V> = Record<string, V>`; `type S = Record<string, number>` | works: alias resolution re-enters `resolveNamedType` |
| user `type Record<…>` / interface / class named `Record` | shadows the builtin (checked before the builtin branch) |

*Closed, no arbitrary props:* dot/member access on the canonical type
routes `memberOfProbe` → `isCollectionsClass` → `collectionsMember`
(member_check.cpp:218): only `set/get/has/delete/clear/keys/values/
entries/forEach/size`; any other property is CP3305 with that
candidate list. `r.foo = …` therefore fails the same way.
*Assignability:* frozen nominal equality — `Record<string, V>` accepts
only `Map<string, V>` (and vice versa); no covariance, no object-shape
conformance, matching `Map` today.
*Construction:* `let r: Record<string, number> = new Map<string, number>();`
and passing a `Record`-typed value where `Map<string, number>` is
expected (and vice versa). See limitations (§7) for object literals.

## 4. What already works with zero further changes

- Type resolution in every annotation position (variables, parameters,
  returns, class fields, tuples/unions/arrays of records, generic
  substitution via `substituteTypeParams` on the canonical parts).
- Member typing + CP3305 candidate lists + Map docs (`stdInstanceMemberDoc`
  / `stdInstanceMemberNames` for family `"Map"` in std_check.cpp — no
  `Record` rows needed, the type *is* `Map`).
- `for…of` over a record: iterator element is the `$mapEntry` `[K, V]`
  tuple via the existing Map path (stmt_check.cpp:632).
- Codegen C++ classification: `classifyCollections`
  (`src/std/collections/codegen.cpp:66`) already emits
  `::cp3::runtime::collections::Map<std::string, V>` for the canonical
  type. No codegen type-mapping change is required.

## 5. Diagnostics (no new codes; `diagnostic_codes.hpp` untouched)

- Wrong arity → existing `kInvalidTypeArgument` (CP3418), message:
  `Record<K, V> requires exactly two type arguments (Record<string, V>)`.
- Non-string key → existing `kIndexSignatureBanned` (CP3803), message:
  `Record<K, V> requires K to be string; use Map<K, V> for non-string keys`.
  CP3803 is deliberately retained here: it is the index-signature ban,
  and `Record` is the one sanctioned spelling of a string index
  signature — the banned range now means "index signature outside
  `Record<string, V>`".

## 6. Follow-up wiring (not in the sema-only patch)

### 6a. Sema owner — closed index read/write (`member_check.cpp`)

`indexOfProbe`'s `Class` branch currently returns `ambient()` for
`Map` (member_check.cpp:470-475), so `r[k]` reads `ambient` and
`r[k] = v` is unchecked. Both read and write flow through
`indexOf`/`checkAssignToTarget` (ops_check.cpp:520), so one branch
closes both. Insert before `return ambient();` in the `Class` case:

```cpp
if (ot.name == "Map" && ot.parts.size() >= 2) {
    // Record<string,V> / Map<string,V> subscript: a missing key reads
    // `undefined` (same rule as Map.get); a write is checked against V
    // by checkAssignToTarget via this result.
    if (ix.index) {
        Type it = inferExpr(*ix.index);          // already inferred in indexOf;
        if (!isStringish(it) && !isPermissive(it)) // see note below
            report(diagcode::kIndexSignatureBanned, ix.index->span,
                   "index into Record<string, V> must be a string");
    }
    return Type::unionOf({ot.parts[1], Type::undefined()});
}
```

Note: `indexOf` (member_check.cpp:438) already infers `ix.index` and
discards it (`(void)it`); the key-type check belongs there or by
reusing that inferred type — do not double-infer. A string *literal*
index is accepted (`isStringish`). If `Map` index semantics are not
wanted radically, gate the branch on `ot.parts[0].isString()` — same
effect for records, and `Map<number, V>[k]` keeps today's `ambient`
until separately designed. Union/nullable receivers already recurse
through `indexOfProbe`, so `Record<string, V> | undefined` works once
the branch lands.

### 6b. Codegen owner — index lowering (no type-mapping change)

- Type mapping: none. `classifyCollections` (codegen.cpp:66) and the
  member call lowerings key on `Class "Map"` + `parts[0] == string`,
  which the canonical record already satisfies.
- Read `r[k]`: in the index dispatch (`emitIndexExpr` /
  `emitFamilyIndexRead`, generator.h:539-543; `emitGenericIndexRead`,
  generic.cpp:334), when `sema.typeOf(ix)` (the side table,
  type_resolve.cpp:82) is `Class "Map"` with a string key part, emit
  the runtime map lookup equivalent to `r.get(k)` —
  `::cp3::runtime::collections::get(recv, key)` — i.e. reuse the exact
  C++ the `get` member call already lowers to, so read-miss yields
  `undefined` with the same semantics as §4's member surface.
- Write `r[k] = v` / `r[k] += v`: in the index-target path
  (`familyIndexWriteText`, generator.h:548, reached from
  assignment.cpp:360), emit `::cp3::runtime::collections::set(recv, key,
  value)` and return the stored value. Compound/update forms must
  read-modify-write through the same two helpers, not through a
  temporary object property.
- Object-literal initialization (optional, TS parity):
  `let r: Record<string, V> = { a: 1, b: 2 };` — if added, lower to
  `makeMapFromEntries` (collections.hpp:486) with one `[key, value]`
  pair per literal field, and add the sema-side exception in
  `wouldAccept` (`Object → Class "Map" with string key, fresh literal`
  → check each field against `parts[1]`). Deliberately *not* in v1
  (see §7); needs codegen + assignability owner sign-off.

## 7. Limitations (v1, explicit)

1. **Index read/write typing is ambient until §6a lands.** `r[k]`
   reads `ambient` (not `V | undefined`) and `r[k] = wrongType` is
   not yet CP3401. Dot access *is* closed today; subscript is the
   documented gap.
2. **No object-literal initialization.** `Record<string, V>` is a
   `Map`; `{a: 1}` is not assignable to it. Construct with
   `new Map<string, V>()`. (Network `Map<string, string>` header
   literals are a separate codegen special case, unaffected.)
3. **Spelling is erased.** Diagnostics print `Map` (pretty() prints
   the class name), and `typeOf` side-table consumers cannot
   distinguish `Record<string, V>` from `Map<string, V>`. That is the
   price of §2; if TS parity later requires the distinction, switch
   the canonical name to `"Record"`, add it to `isCollectionsClass`,
   and §6a/§6b become the *only* enforcement paths.
4. **No `Record<symbol/bigint/…>`**, no readonly variant
   (`Readonly<Record<…>>`), no `{[key: string]: V}` object syntax.
5. **Key side is invariant `string`:** `Record<string, V>` is not a
   supertype of `Map<string, V>` with a narrower key type — there is
   no narrower string key other than literals, which canonicalize
   to `string`.

## 8. Tests to add (not run in this patch)

Sema doc-test cases (tests/cases), asserting codes only:

- `record_read_write`: `let r: Record<string, number> = new Map<string, number>();`
  `r.set("a", 1); let n: number | undefined = r.get("a"); r.foo;` → CP3305.
- `record_bad_key`: `Record<number, string>` → CP3803.
- `record_arity`: `Record<string>` → CP3418.
- `record_alias`: `type R = Record<string, boolean>;` used as a type.
- `record_shadow`: user `type Record<T> = T[];` — `Record<number>` is
  `number[]`, not a map.
- After §6a: `r["a"]` → `number | undefined`; `r["a"] = "x"` → CP3401;
  `r[1]` → CP3803; `r["a"]` on `Record<string, number> | undefined`.
- After §6b: run the existing collections corpus + a record index
  read/write case end-to-end (generated C++ uses `collections::get/set`).
