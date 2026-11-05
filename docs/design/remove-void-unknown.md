# Remove `void` / `unknown` — ledger K (sema + parser diagnostics)

Status: diagnostics landed in scope (no corpus rewrite, no codegen/CMake
change). The no-return → `undefined` inference patch is specified for the
sema owner in §6 (2 lines outside this change's file scope).

## 1. Problem

Two annotation spellings survive as legacy TypeScript shapes whose
semantics CP3 does not want:

* `void` — "returns nothing / no value". CP3 already has a value type
  for this (`undefined`); `void` adds a second spelling that codegen
  maps separately and normatively differs (`void` is not a value you
  can store, compare, or `typeof`).
* `unknown` — "top type". In CP3 the permissive shape is `object`
  (ambient) and the untyped parse result is an error rather than a
  value, so `unknown` mostly punts the shape question to the reader.
  `JSON.parse(text)` returning `unknown` is the motivating case: the
  caller must narrow at every use site.

## 2. Decision

Three diagnostics, all compile errors, all naming the fix in the
message (the `help:`-style suggestion rides in the message text, per
the `kAnyTypeBanned` precedent in `banned.cpp`).

| Form | Code | Message (fix named) | Where emitted |
|---|---|---|---|
| `: void` annotation (any position) | CP3420 `kVoidTypeBanned` | `the 'void' type is removed; annotate the return as 'undefined' instead` | `sema/src/checks/type/type_check.cpp`, `Analyzer::resolveNamedType` |
| `: unknown` annotation (any position) | CP3421 `kUnknownTypeBanned` | `the 'unknown' type is removed; use a concrete type, or 'object' for an untyped value` | same choke point |
| `JSON.parse(x)` without `<T>` | CP3208 `kJsonParseMissingTypeArgument` | `JSON.parse requires a type argument; use JSON.parse<T>(x)` | `parser/src/expr/primary.cpp`, `ParserImpl::parseMemberAccess` (interim — §6) |

No-return functions infer `undefined` (not `void`): §6.

## 3. Semantics (v1)

### `: void` → `undefined`

`Analyzer::resolveNamedType` intercepts the `void` spelling before the
primitive lookup and resolves it to `Type::undefined()`. Consequences:

* **Return annotations** (`function f(): void`): now `f` is `() =>
  undefined`. Bare `return;` stays legal (existing
  `analyzeReturn` treats `undefined` like `void` there); `return 1`
  now reports CP3401 against `undefined` *in addition to* CP3420 at
  the annotation (dual diagnostic until the annotation is fixed).
* **Variables/parameters/fields/type members** (`let x: void`,
  `list: void[]`, `value: void`): resolve to `undefined`-based types,
  so the rest of the program still type-checks against the intended
  meaning (no cascade, no codegen hole — `voidType()` has no lowering
  for stored values anyway).
* **`Promise<void>`** (async returns) resolves to
  `Promise<undefined>`, which lowering already handles.
  `async function f(): Promise<void>` is spelled
  `async function f(): Promise<undefined>` (or
  `async function f(): undefined`, which async wraps).
* A user type or type parameter named `void` is still unnameable —
  same precedence as the old primitive lookup (the keyword token
  wins), so no behaviour change beyond the diagnostic.

The unary `void` *operator* (`void x`, `expr_check`/`UnaryOp::Void`)
is an expression, not an annotation spelling, and is **not** removed
by this change. If the operator is meant to go too, that is a
separate ledger item.

### `: unknown` → removed

The spelling is rejected; the resolution keeps the internal
`Type::unknown()` sentinel so downstream checks short-circuit exactly
as they did for an accepted `unknown` annotation (the program already
has an error; codegen never sees a new type). Internal
`Type::unknown()` values produced by inference/error paths are
unaffected — only the source spelling errors.

Replacement rules:

| Was | Write instead |
|---|---|
| `x: unknown` (value of unknown shape) | `x: object` |
| `f(...items: unknown[])` (rest param) | `f(...items: object[])` or a concrete element type |
| `f(): unknown` (annotated return) | drop the annotation (inference) or name the concrete type |
| `JSON.parse<unknown>(x)` | `JSON.parse<T>(x)` with the shape; a bare `unknown` element is `object` |
| `x: unknown \| null` | `x: object \| null` (member access is permit-checked as ambient, see §7) |

### `JSON.parse<T>`

Untyped `JSON.parse(text)` is a compile error (CP3208, parser, interim
location — §6). Typed `JSON.parse<T>(text)` is unchanged: sema
`tryInferJsonCall` already resolves the type argument and returns
`T`; `JSON.stringify` is unaffected. In the parser stage the check
is syntactic — callee `MemberExpr` whose object is the identifier
`JSON` and whose property is `parse` — mirroring sema's own
`tryInferJsonCall` recognition, so a *shadowed* user binding named
`JSON` with a `parse` method is flagged too (documented
limitation, §7).

### No-return functions

Today an unannotated no-return body infers `Type::voidType()`
(`checks/stmt/stmt_check.cpp:758` for declarations,
`checks/expr/expr_check.cpp:569` for arrows). With `void` retired as
an annotation, the inferred type must be `undefined` (async: the
existing `promiseOf(...)` wrap then yields `Promise<undefined>`).
Both sites are outside this change's file scope; the exact
lead patch is §6.1. Until it lands, a no-return function's inferred
type still prints as `void` in cross-references, although nobody can
*write* `: void` any more.

## 4. Diagnostic codes

| Code | Name | Range owner |
|---|---|---|
| CP3420 | `kVoidTypeBanned` | types (sema) |
| CP3421 | `kUnknownTypeBanned` | types (sema) |
| CP3208 | `kJsonParseMissingTypeArgument` | syntax (parser) |

Codes are appended, never renumbered (frozen interface). CP3208
lives in the 32xx range because the parser emits it today; when the
check moves to sema (`tryInferJsonCall`, §6.2) the code keeps its
number — codes are stable identifiers, not stage labels.

## 5. Migration mapping

| Corpus form | Rewrite |
|---|---|
| `…: void {` (function/method/accessor) | `…: undefined {` |
| `() => void` function type | `() => undefined` |
| `Promise<void>` | `Promise<undefined>` |
| `let x: void` / `x: void` field / `void[]` | drop the annotation (infer), or `undefined` / `undefined[]` |
| `type T = void` | `type T = undefined` |
| `…: unknown` | drop the annotation, or a concrete type, or `object` |
| `unknown[]` (incl. rest params) | `object[]` or concrete element |
| `JSON.parse(t)` | `JSON.parse<T>(t)` |
| `res.json()` if it returns untyped JSON value | `res.json<T>()` (same pattern; generic_check already supports it — verify per API) |

Error-case test fixtures that exercised `void`/`unknown` semantics
(e.g. `functions-closures/03/void/*`,
`classes/04/diagnostics/field_unknown_error`,
`async/01/async/async_unknown_inferred`,
`stdlib/02/json/parse/untyped/*`) need new expectations: they are no
longer behavioral tests of a legal form. Lead-owned crew update (§6.3);
`void_return_value_error` etc. may now show the dual CP3420+CP3401
pair (§3).

## 6. Lead patch list (outside this change's scope)

1. **No-return inference → `undefined`** (the only behavior change
   still to land):
   * `src/sema/src/checks/stmt/stmt_check.cpp:758`
     `? Type::voidType()` → `? Type::undefined()`
   * `src/sema/src/checks/expr/expr_check.cpp:569`
     `? Type::voidType()` → `? Type::undefined()`
   Then greppable `Type::voidType()` users in sema/codegen can be
   retired phase by phase; codegen keeps a `Void` lowering as long
   as those remain.
2. **Move the `JSON.parse` check into sema**:
   in `tryInferJsonCall`
   (`src/sema/src/checks/call/call_check.cpp:433`), for
   `property == "parse"` with `call.typeArgs.empty()`, report
   CP3208 at `call.span` and return `Type::undefined()`; then delete
   the parser-side helper in `primary.cpp`. The parser check stays
   as a stopgap (it fires even when the program would not reach sema
   inference in a degraded pipeline); moving it removes the
   shadowed-`JSON` false positive (§7).
3. **Corpus + docs sweep**: rewrite the forms in §5, re-baseline
   affected test expectations (dual-diagnostic cases in §3),
   update `docs/standard-library` / language-spec mentions of
   `: void` / `unknown` and the `ban` list in
   `banned.cpp`'s "use 'unknown'" hint (it currently recommends the
   now-banned spelling — flip to "use a concrete type or 'object'"
   when the lead touches that file).
4. **Arrow double-report cleanup**: `inferArrowReturn`
   (`expr_check.cpp:576`) resolves an arrow return annotation a
   second time, so an arrow annotated `: void` / `: unknown`
   reports twice. Remove the re-resolve (or thread the first
   resolution through) with §6.1. Function declarations resolve once
   (`buildSig`) and are unaffected.

## 7. Limitations

* The parser `JSON.parse` check is syntactic (callee-name based):
  it cannot tell std `JSON` from a user variable named `JSON`, and
  it runs before imports resolve. It is interim (§6.2).
* `JSON?.parse(x)` (optional call) is checked; `JSON?.parse<T>(x)`
  relies on the postfix `<…>` branch picking up the type args as a
  separate call — quirky but functional; verify in tests.
* The unary `void` operator (`void x`) is untouched (§3).
* `never` (still mapped to `Type::unknown()` by
  `primitiveTypeOf`) is untouched by this change.
* Interface/class method *signature* diagnostics for `unknown`
  inside `.d.ts`-style declaration files emitted by tooling are
  subject to the same CP3421 error; emitter owners should stop
  spelling `unknown` (§5).

## 8. §K finish — landed (sema, scoped)

Implemented by the §K owner; code only (no build/test, no commit).
Scope: `src/sema/src/checks/stmt/stmt_check.cpp`,
`src/sema/src/checks/expr/expr_check.cpp`,
`src/sema/src/checks/call/call_check.cpp` only (§6.1/§6.2/§6.4).

* **§6.1 no-return → `undefined`** `stmt_check.cpp:758`
  (`Type::voidType()` → `Type::undefined()` for unannotated
  declarations) and `expr_check.cpp` block-body arrows (same swap).
  Async follows via the existing `promiseOf(...)` wraps →
  `Promise<undefined>`. `Type::voidType()` survivors elsewhere in sema
  (member_check, class, promise_call, call_check timers, type_check
  sentinel) are untouched — retire phase by phase.
* **§6.4 arrow double-report cleanup** `expr_check.cpp::inferArrowReturn`:
  removed the dead first `resolveType(*arrow.returnType)` on the
  expression-body branch (`else if (arrow.returnType)`), which was
  immediately overwritten by the trailing resolve; the annotation now
  resolves exactly once for both body shapes. A `: void` / `: unknown`
  arrow annotation therefore reports CP3420/CP3421 once, matching
  function declarations (`buildSig`).
* **§6.2 `JSON.parse` missing-arg check moved to sema**
  `call_check.cpp::tryInferJsonCall`: `property == "parse"` with
  empty/`null` type args now reports CP3208 at `call.span` and returns
  `Type::undefined()`; typed `JSON.parse<T>(x)` and
  `JSON.stringify` are unchanged (argument checking still runs via
  `checkArgs`).
* **Parser helper NOT deleted (out of scope)**: deleting
  `isBareJsonParse` + its three call sites in
  `src/parser/src/expr/primary.cpp` was not permitted under the
  file-scope constraint for this change, so the interim parser
  CP3208 currently **duplicates** the new sema CP3208 for
  `JSON.parse(x)`. Lead follow-up (must land with this to avoid
  double diagnostics): delete the parser helper in `primary.cpp`
  (the `isBareJsonParse` block and its uses in the `?.` call,
  `(` call, and `<…>`-type-args call branches) and re-baseline the
  `stdlib/02/json/parse/untyped/*` fixtures to a single CP3208.

Behavior changes: untyped `JSON.parse` value type `unknown` →
`undefined` (all use sites now see `undefined`); unannotated
no-return functions/arrows print/assign as `undefined` instead of
`void`; arrow `: void`/`: unknown` annotations now single-report.

Limitations: no build/test run (per scope); `Type::voidType()`
lowering stays until the remaining sema/codegen users are retired;
shadowed-`JSON` false positive remains until the parser helper is
deleted (sema recognition is still name-based, as before); the
`void` operator, `never`, and §6.3 corpus/docs sweep are untouched.
