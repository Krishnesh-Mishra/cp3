# `Record<string, V>` — index read/write codegen

Follow-up to `record-index-signatures.md` §6 (sema type-resolution landed;
this note closes the §6a sema probe + §6b codegen lowering). Scope is
deliberately narrow: reads, writes, compound writes and `++`/`--` on the
canonical `Map<string, V>` type. No type-mapping change, no new
diagnostic, no object-literal initialization.

## Files

| File | Change |
|---|---|
| `src/sema/src/checks/expr/member_check.cpp` (`indexOfProbe`, `Class` branch only) | `Map` (string-key part) subscript types to `V \| undefined`; non-string key is CP3803 (see §1). |
| `src/codegen/src/emit/expr/expr.cpp` (`exprPlain` `IndexExpr` dispatch, `emitUpdateIndex`, `isStringKeyMap` helper) | `r[k]` read + `r[k]++`/`--` lowering. |
| `src/codegen/src/emit/control/assignment.cpp` (`AssignEmitter::mapIndexWrite`, `isStringKeyMapTarget` helper, one dispatch call) | `r[k] = v` + compound lowering. |
| `docs/.rough/design/record-index-codegen.md` | this note. |

Nothing else touched (`generator.h`, `type_map.h`, `typed-arrays`,
`collections/codegen.cpp`, build files unchanged). The helpers are
file-local predicates over `Type`; `mapIndexWrite` lives on
`AssignEmitter` (defined in the `.cpp`, already a `Generator` friend), so
no header edit is needed.

## Lowering table

`r` is a non-nullish `Class "Map"` with a string-ish key part
(`parts[0]` is `string`, a string literal, or a type parameter — the
`Record<string, V>` canonical form). Detection is via
`effectiveType(*r)`; `needsCollections_` is set, so
`cp3/std/collections.hpp` is included. All forms reuse the exact C++
the `r.get(k)` / `r.set(k, v)` member calls already emit
(`emitCollectionsMemberCall`): `(recv)->get(key)` is
`MapData::get` → `std::optional<V>` (miss → `std::nullopt` →
`undefined`); `recv->set(key, value)` is `MapData::set` (`void`). There
is no free `collections::get/set` (the design sketch's spelling); the
member form is the real ABI.

| Source | Emitted C++ (abbreviated) | Result type / value |
|---|---|---|
| `r[k]` (read) | `(expr(r))->get(expr(k))` | `optional<V>` ≡ `V \| undefined`; miss → `undefined` |
| `r[k] = v` | `([&]{ auto cp3_m=expr(r); auto cp3_k=expr(k); auto cp3_v=expr(v); cp3_m->set(cp3_k,cp3_v); return cp3_v; }())` | `V` (the stored value) |
| `r[k] += v` / `-=` / `*=` / `/=` (number) | IIFE: `auto cp3_o=cp3_m->get(cp3_k); auto cp3_old=cp3_o.value_or(NaN); auto cp3_new=(cp3_old OP cp3_r); cp3_m->set(cp3_k,cp3_new); return cp3_new;` | `V` (JS: `undefined OP v` with `number` coercion) |
| `r[k] %= v` / `**= v` (number) | same IIFE; `%` via `Generator::modCall`, `**` via `std::pow` | `V` |
| `r[k] &= v` / `\|= v` / `^= v` / `<<= v` / `>>= v` / `>>>= v` (number) | same IIFE; `::cp3::runtime::bitwise::{band,bor,bxor,shl,shr,ushr}(cp3_old,cp3_r)`, seed `0.0` | `V` |
| native-integer `V` compounds | `::cp3::runtime::checked::{add,sub,mul,div,mod}<V>(cp3_old,cp3_r)` for arithmetic; raw `& \| ^ << >>` for bitwise/shift (mirrors the identifier path); seed `0` | `V` |
| `r[k] &&= v` / `\|\|= v` | IIFE with deferred rhs: `if (has && cp3_truthy(*o)) { set(rhs); } return get;` (rhs text only inside the taken branch) | `optional<V>` (keeps miss as `undefined` when skipped) |
| `r[k] ??= v` | `if (o.has_value()) return *o; set(rhs); return rhs;` | `V` (sema `stripNullish` + `unify`) |
| `r[k]++` / `--`, `++r[k]` / `--r[k]` | IIFE read-modify-write through `get`/`set`; missing seed `NaN` (number) or `0` (native); native uses `checked::{add,sub}`; prefix returns new, postfix returns old | `number` / native per `inferUpdate` |

Single-evaluation rules: receiver and key are bound once (`cp3_m`,
`cp3_k`) before the read; the assigned/rhs value is bound once
(`cp3_v`/`cp3_r`). For arithmetic compounds the `get` runs before the
rhs text (JS order: target ref, old value, rhs, put). For `&&=` /
`||=` / `??=` the rhs text appears only in the branch that needs it, so
an effectful rhs runs exactly when the short-circuit demands it;
receiver/key are still evaluated once. The simple-assign IIFE returns
the bound `cp3_v` rather than `(set(...), v)` so `v` is not evaluated
twice. `v`/`k` are emitted with `expected_ == V` (`parts[1]`) so
literals and native integers spell in the map's value type.

`r[k]` in any value position (binary operands, call args, `return`,
templates, `typeof`) goes through the same `exprPlain` dispatch; the
result's C++ is `optional<V>`, and its cp3 type comes from sema's side
table (`V | undefined`), so downstream `classify` already sees
`Optional`.

## Sema (§6a) wiring notes

`indexOfProbe`'s `Class` branch, before the `ambient()` fallthrough:

```cpp
if (ot.name == "Map" && ot.parts.size() >= 2) {
    // gated on string-ish key part (see below)
    if (ix.index) {
        Type it = typeOfExpr(*ix.index);            // reuse, don't re-infer
        if (it.isUnknown() && no side-table entry) it = inferExpr(*ix.index);
        if (!isStringish(it) && !isPermissive(it))
            report(kIndexSignatureBanned, ix.index->span,
                   "index into Record<string, V> must be a string");
    }
    return Type::unionOf({ot.parts[1], Type::undefined()});
}
```

- Both read and write flow through `indexOf` / `checkAssignToTarget`
  (`ops_check.cpp`), so one branch closes both: a read is `V |
  undefined`; a write `r[k] = wrongType` is CP3401 via `checkAssign`
  against that result. Union/nullable receivers (`Record<string,V> |
  undefined`) already recurse through `indexOfProbe` per part, so they
  work once the branch lands (narrowing still required before
  codegen — see Limitations).
- **No double-infer.** `indexOf` already infers `ix.index` and discards
  it. The probe reuses it via `typeOfExpr` (the `nodeTypes` side
  table) and only calls `inferExpr` when no entry exists (a direct
  probe bypassing `indexOf`). A string-literal key is accepted
  (`isStringish`); `unknown`/`error`/ambient keys are permissive (no
  CP3803).
- **Gate on the key part.** The branch only applies when `parts[0]` is
  `string`, a string literal, a `TypeParam`, or `unknown`.
  `Map<number, V>[k]` keeps today's `ambient` until separately
  designed (the §6a NOTE's conservative option). This is the
  `isString()` gate the design note suggests, extended to literals and
  the deferred `Record<K, V>` type-parameter case.
- **No new diagnostic.** CP3803 (`kIndexSignatureBanned`) is retained:
  for `Record` the banned range now means "index signature outside
  `Record<string, V>`" plus "non-string key into one".

## Codegen wiring notes

- **Read** is intercepted in `exprPlain` (`IndexExpr` case) before
  `emitIndexExpr`, so `emitFamilyIndexRead` (typed arrays / `Array` /
  `String`) is untouched. A non-`Map` receiver falls through to the
  existing diagnostic (`kNotIndexable`).
- **Write** is intercepted in `Generator::emitAssignment` before
  `emitFamilyIndexWrite`; `AssignEmitter::mapIndexWrite` returns `""`
  for any non-`Map` target, so array/typed-view/tuple writes are
  unchanged. Compound ops therefore do not fall into the
  "invalid assignment target" error.
- **Update** (`r[k]++`) is intercepted at the top of `emitUpdateIndex`
  before the typed-array/array branches.
- Nullable/union receivers do not match `isStringKeyMap` (exact
  non-nullish `Map` only). The member-call surface has the same rule, so
  `r: Record<string,V> | undefined; r[k]` is a sema success but needs a
  narrowing (`if (r)` / `r!`) or `?.` before codegen will lower it.
  The optional-Map C++ (`std::optional<Ref<...>>`) has no `->get`
  spelling; synthesizing a null guard here would change the
  `undefined`-vs-throw contract, so it is left as a diagnostic.

## Limitations

1. **Compound on a possibly-missing key is often a sema error.**
   `r[k] += v` with `r[k]: number | undefined` runs through
   `evalBinary(Add, number|undefined, v)` → CP mixed-type-arithmetic,
   because `isNumberish(union)` is false. Programs must establish the
   key (`r.set(k, 0)` first, `r.has(k)` guard + non-null assertion, or a
   dedicated narrowing pass) before compounding. The codegen path exists
   and is correct when sema accepts the compound; the missing seed
   (`NaN` for `number` arithmetic, `0` for bitwise/native) matches JS
   `undefined` coercion for the cases that reach it (`++/--` above).
   String `+=` on `Record<string,string>` is likewise rejected by sema
   (`string | undefined` is not `string`); there is no implicit
   `"undefined"` stringification, matching CP3's `string+string`-only
   `+`.
2. **`r[k] = undefined` is currently accepted.** `checkAssign(undefined,
   V | undefined)` succeeds via the `undefined` arm of `wouldAccept`.
   Strictly `set` wants `V`; the permissive write is inherited from
   typing the probe as `V | undefined` for both directions (§6a's
   "checked against V via this result" is approximate). Rejecting it
   would need a write-side override in `checkAssignToTarget`, outside
   this patch's `indexOfProbe`-only scope.
3. **`Map<number, V>` subscript stays `ambient`** (gate above). Use
   `r.get(k)` / `r.set(k, v)` there, or extend the probe to numeric
   keys.
4. **No object-literal init** (`let r: Record<string,V> = {a: 1}` still
   fails; construct with `new Map`). Part of §6b's "optional" item;
   deliberately out of v1.
5. **Spelling erased.** Diagnostics print `Map` and the side table
   cannot distinguish `Record<string,V>` from `Map<string,V>` (§2 price).
6. **No `delete r[k]` / `Reflect` / mandatory `in` changes.** `in`
   already lowers to `has`; use `r.delete(k)` for removal.
