# Built-in utility types, generic constraints/defaults, `const enum`

Status: sema + parser + public model landed (this patch). Codegen lowering
for `const enum` inlining (and the already-known `Record` index lowering of
`record-index-signatures.md` §6b) is the codegen lead's follow-up (§6).
No new diagnostic code, no runtime metadata, no std-module or CMake change.

## 1. What was planned

ROADMAP "Planned" rows:

| Row | This patch |
|---|---|
| Built-in utility types `Partial`, `Required`, `Readonly`, `Pick`, `Omit`, `Record` | §2 |
| Generic constraints and defaults `<T extends X = Y>` | §3 |
| `const enum` and enum reverse mapping | §4 |

The public vocabulary lives in `src/common/include/cp3/core/utility_types.hpp`:
`UtilityKind` / `utilityKindOf` / `utilityArity`, `GenericParamSpec`,
`ConstEnumValue` / `ConstEnumReverseEntry` / `ConstEnumInfo`. Sema
publishes enum values in the formerly deferred `enumInfo` table,
`SemaResult::constEnums` (`cp3/core/analysis.hpp`, keyed by declaration
nodeId); everything else resolves into the ordinary `Type` / node-type
side tables, so no new side table was needed for utilities.

## 2. Utility types — resolved structurally in `resolveNamedType`

All six names are recognized in `Analyzer::resolveNamedType`
(`src/sema/src/checks/type/type_check.cpp`), *after* the user
alias/interface/`typeNames_` lookups — so a user-declared `Partial` (alias,
interface or class) shadows the builtin, exactly as `Record` already
behaved. Each resolves to a plain structural `Type`:

| Form | Resolved form |
|---|---|
| `Partial<T>` | object shape of `T` with every field `optional = true` |
| `Required<T>` | same, every field `optional = false` |
| `Readonly<T>` | same fields/optionality under a `$ro{...}` shape label — a *distinct* `Type` from the mutable shape (`Type::operator==` differs) whose shape fields are identical, so assignability walks it exactly like the mutable shape (mutually assignable, TypeScript-style) |
| `Pick<T, K>` | shape keeping only fields named by `K` |
| `Omit<T, K>` | shape dropping fields named by `K` (the type-level counterpart of the existing `omitShape` rest-pattern operation) |
| `Record<K, V>`, `K` a union of literals | object shape with exactly those fields, each of type `V` |
| `Record<string, V>` | unchanged: canonical `Map<string, V>` (see `record-index-signatures.md`) |

Semantics detail:

- **Distribution.** `Union` and `Nullable` sources distribute member-wise
  (`Partial<A | B>` = `Partial<A> | Partial<B>`, `Partial<T | null>` =
  `Partial<T> | null`), mirroring TypeScript.
- **Keys.** `K` must be a string/number literal or a union of them
  (`"a" | "b"`). Anything else (bare `string`, a type parameter) is
  **CP3401** (`kTypeMismatch`): `Pick/Omit keys must be string literal
  types (for example "a" | "b")`, resolving to `unknown`.
- **Class sources** are enumerated from the class member table (instance
  members; methods keep their function type), with the source's type
  arguments substituted into member types first, so
  `Partial<Box<number>>` sees `number`, not `T`. The result is a
  structural object shape.
- **Type-parameter sources** transform their `extends` constraint when it
  is an enumerable shape; an unconstrained parameter passes through
  untouched (permissive, as elsewhere).
- **Open shapes** (ambient/computed): `Partial`/`Required`/`Readonly`
  return the source unchanged (the member set does not change);
  `Pick`/`Omit` resolve to `unknown` (the member set is unknowable).
- **Tuples/arrays/primitives** pass through for
  `Partial`/`Required`/`Readonly` (`Partial<number>` is `number` in
  TypeScript too); `Pick`/`Omit` over them resolve to `unknown`.
- **Arity.** A wrong type-argument count is **CP3418**
  (`kInvalidTypeArgument`), the existing code `Record` already used:
  `Partial requires exactly 1 type argument(s); got 2`.
- **No runtime metadata.** The resolved shape is published through the
  ordinary `resolveType` side table (`sema.typeOf`); codegen reads it
  exactly like any other object shape. There is no mapped-type syntax, no
  `$partial`-style runtime tag a consumer must interpret.

### Resolved-form examples

```cp3
type User = { name: string, age: number };
type P = Partial<User>;    // { name?: string, age?: number }
type R = Required<P>;      // { name: string, age: number }
type RO = Readonly<User>;  // { readonly name: string, readonly age: number }
type N = Pick<User, "name">;   // { name: string }
type O = Omit<User, "age">;    // { name: string }
type M = Record<"x" | "y", number>;  // { x: number, y: number }
type D = Record<string, number>;     // Map<string, number> (unchanged)
```

## 3. Generic constraints and defaults `<T extends X = Y>`

**Parser (already landed before this patch, verified):**
`parseTypeParamsIfPresent` (`src/parser/src/decl/declarations.cpp`) parses
`<T extends X = Y>` into `TypeParameter{ name, constraint, defaultType }`
for functions, classes, interfaces, aliases, arrows and function types;
node ids cover both children (`node_ids.cpp`).

**Sema — what this patch adds (all in `resolveNamedType`, type_check.cpp):**

1. **Arity at instantiation.** Excess type arguments for a generic alias,
   interface or known generic class are **CP3418**:
   `type alias 'Pair' expects 1 type argument(s); got 2`. A bare use
   (`Pair` with no arguments) keeps today's permissive behaviour.
2. **Shape-aware substitution for aliases.** The existing
   `substituteTypeParams` (declarations.cpp) only walks `Type::parts`, so
   `type Box<T> = { value: T }` used as `Box<number>` kept a bare `T`.
   The alias branch now substitutes inside object/tuple shapes too, so
   `Box<number>` is the shape `{ value: number }` (matching
   `resolveGenericInterface`).
3. **Constraint/default model.** `GenericParamSpec`
   (`utility_types.hpp`) is the resolved record of one type parameter:
   `constraint` + `hasConstraint`, `defaultType` + `hasDefault`. The
   convention for a `TypeKind::TypeParam` `Type` is unchanged:
   `parts[0]` (when present) is the constraint (generic_check.cpp
   already enforces it for generic *function calls*, CP3401).

**The honest gap (why constraint/default *enforcement* for aliases,
interfaces and classes is not in this patch).** Recording a spec requires
the resolved constraint/default `Type`s, which are only available from the
`TypeParameter` AST nodes at declaration time — in
`declareTypeAlias` / `registerInterface` / `resolveClass`
(declarations.cpp / class_check.cpp), none of which are in this patch's
allowed file set. Today those passes record parameter *names* only
(`typeAliasParams_`, `GenericInterface::typeParams`,
`ClassInfo::typeParamNames`). The consumer side (§3.2–3.3 above) is live;
the missing piece is exactly this producer wiring (§6b): resolve each
`TypeParameter`'s `constraint`/`defaultType` once at declaration and store
a `std::vector<GenericParamSpec>` next to the name lists. Until then, a
missing alias/interface/class type argument stays a bare (permissive)
type parameter, and a provided argument is not compared against a
constraint — the same permissive behaviour as before, now with arity
checked.

## 4. `const enum` values and the reverse map

**Parser.** `const enum Color { ... }` (including `export const enum …`):
`parseDeclaration` accepts `KwConst` + `KwEnum`, and
`parseEnumDeclaration` consumes the `const` and sets the new
`EnumDeclaration::isConst` field (`cp3/core/ast.hpp`).

**Sema (`registerEnum`, enum_check.cpp).** Every enum declaration —
regular and `const` alike — is resolved into a `ConstEnumInfo` published
in `SemaResult::constEnums` (keyed by declaration nodeId):

- **Values.** Each member's constant expression is *evaluated*
  (`evalConstNumber` / `evalConstString`): literals, unary sign/`~`,
  arithmetic (`+ - * / %`, string `+`), and links to earlier members
  (`B = A`, `B = Color.A`). Implicit numeric members auto-increment
  (first is `0`, then previous + 1) — the same rule the codegen
  evaluator reads for regular enums. Non-constant initializers keep
  today's CP3401 classification diagnostic and stay valueless.
- **Reverse map.** Numeric-backed enums only (TypeScript parity:
  string-backed enums have none): `value -> member name`, with duplicate
  values collapsing to the *last* member with that value (each member
  overwrites the previous entry, as TypeScript's reverse mapping does).
- **No runtime metadata.** For a `const enum` the table *is* the
  semantics: codegen inlines `Color.Red` to `0` (and `Color[0]` to
  `"Red"`) from it and emits no runtime object. Regular enums keep
  their runtime object; the table is equally available to their
  codegen.

## 5. Diagnostics (no new codes)

| Situation | Code | Message sketch |
|---|---|---|
| Utility with wrong argument count | CP3418 (`kInvalidTypeArgument`) | `Partial requires exactly 1 type argument(s); got 2` |
| `Pick`/`Omit` keys not string literals | CP3401 (`kTypeMismatch`) | `Pick keys must be string literal types (for example "a" \| "b")` |
| Generic alias/interface/class with excess type arguments | CP3418 | `type alias 'Pair' expects 1 type argument(s); got 2` |
| `Record` non-string key | CP3803 (unchanged) | existing message |
| Non-constant enum initializer | CP3401 (unchanged) | existing message |

## 6. Wiring needed (not in this patch)

### 6a. Codegen lead — `const enum` inlining + `Readonly` writes

- **Type mapping: none.** Utility types resolve to ordinary shapes /
  the canonical `Map` (Record, per `record-index-signatures.md` §4).
  `Readonly<T>` is an object shape whose label starts with `$ro{` —
  codegen maps it to the same C++ struct as the mutable shape.
- **`const enum` member reads:** when the receiver of a `MemberExpr`
  resolves (via the symbol / `sema.typeOf`) to an enum whose declaration
  has a `ConstEnumInfo` with `isConst == true`, emit the member's
  `numberValue` / `stringValue` as a literal instead of consulting a
  runtime object. There is no runtime object to consult.
- **Reverse-map reads:** `Color[0]` on a `const enum` with a numeric
  reverse entry emits the entry's `name` as a string literal. A numeric
  index with no entry reads `undefined` (same rule as a miss).
- **`Readonly` writes:** the distinct `$ro` label is the signal. A write
  target (`x.name = …`, `x.name++`, …) whose type is a `$ro` shape is
  rejected at emission (or, preferably, in sema's assignment checker —
  `wouldAccept` deliberately stays assignable, so the check belongs on
  the *target* side, next to the existing class `readonly` rejection in
  `userClassMember`, class_check.cpp:614). Until that lands, readonly
 -ness is type-level only and writes still emit — the documented gap.

### 6b. Sema lead — generic spec producer (activates §3.3)

In `declareTypeAlias` (support/declarations.cpp), `registerInterface`
and `resolveClass`, resolve each `TypeParameter`'s `constraint` and
`defaultType` once (the same way `registerTypeParams`, scopes.cpp:369,
already resolves a function's constraint) and store
`std::vector<GenericParamSpec>` beside `typeAliasParams_` /
`GenericInterface` / `ClassInfo`. Then, at the instantiation sites in
`resolveNamedType`, compare each provided argument against its spec's
constraint (**CP3412**, `kConstraintNotSatisfied` — the existing code
for constraint violations) and fill missing arguments from the spec's
default. Generic *function calls* already check constraints
(generic_check.cpp); function *defaults* need the same fill in
`inferGenericCall` (a parameter bound by neither an explicit argument
nor inference takes its default instead of staying a bare `TypeParam`).
The `GenericParamSpec` shape and the `parts[0]`-is-constraint convention
are already defined, so this is fill-in-the-producer work, not design.

### 6c. Sema lead — enum index typing (reverse-map reads)

`Color[0]` currently flows through `indexOfProbe` (member_check.cpp),
which returns `ambient()` for enum receivers. Once §6a exists, type a
numeric index into a numeric-backed enum as `string | undefined` (the
reverse-map read) and a string index into any enum as the member value
type — one branch in `indexOfProbe`, mirroring §6a of
`record-index-signatures.md`.

## 7. Limitations (explicit)

1. **`Readonly` is type-level only** until §6a lands: the `$ro` label
   distinguishes the type, but writes are not yet rejected.
2. **Constraints/defaults for aliases, interfaces and classes** are
   parsed and modelled but not yet enforced (§3, §6b). Function calls
   enforce constraints today; function defaults are not yet filled.
3. **`Partial`/`Required`/`Readonly` over tuples** keep the tuple
   unchanged (tuple elements have no per-element optionality in the
   shape model); `Pick`/`Omit` over a tuple resolve to `unknown`.
4. **`Pick`/`Omit`/`Partial` over a generic class instantiation**
   substitutes type arguments into *field* types only; method signatures
   are carried unsubstituted.
5. **Open-shape utilities:** `Pick`/`Omit` over an ambient/computed shape
   resolve to `unknown` (the field set is unknowable); there is no
   `{[key: string]: V}` syntax (CP3803, unchanged).
6. **`Record` spelling erasure is unchanged** for the open form:
   `Record<string, V>` prints and compares as `Map<string, V>` (§2 of
   `record-index-signatures.md`). The closed literal-union form is a
   structural shape and keeps its fields.
7. **String-enum reverse mapping does not exist** (TypeScript parity);
   `Color["Red"]`-style forward reads on string enums type through the
   ordinary enum member path.
8. **Native-binding enums** (`import native …`) do not populate
   `constEnums` (their member values live in the binding, not in cp3
   source).

## 8. Tests to add (not run in this patch)

Sema doc-test cases (`tests/cases`), asserting codes only:

- `utility_partial_required`: `Partial<User>` / `Required<Partial<User>>`
  used as annotations; a value missing a now-optional field is accepted,
  a value missing a required field is CP3410.
- `utility_readonly_pick_omit`: `Readonly<User>` annotation; `Pick<User,
  "name">` rejects `{ age: 1 }` (CP3409 fresh-literal); `Omit<User, "age">`
  accepts `{ name: "a" }`.
- `utility_record_closed`: `Record<"x" | "y", number>` accepts
  `{ x: 1, y: 2 }`, rejects `{ x: 1 }` (CP3410).
- `utility_arity_keys`: `Partial<User, User>` → CP3418;
  `Pick<User, string>` → CP3401.
- `utility_shadow`: `type Partial<T> = T[];` — `Partial<number>` is
  `number[]`, not an object shape.
- `generic_arity_shape`: `type Box<T> = { value: T };` — `Box<number>`
  accepts `{ value: 1 }`, rejects `{ value: "x" }` (CP3401);
  `Box<number, string>` → CP3418.
- `const_enum_values`: `const enum Color { Red, Green = 5, Blue }` —
  `Color.Red`/`Color.Blue` type as `Color`; the `SemaResult::constEnums`
  entry for the declaration carries values `0, 5, 6` and reverse
  `0 -> Red, 5 -> Green, 6 -> Blue` (assert against the side table in a
  sema unit test, not a doc test).
- `const_enum_reverse_dup`: `const enum E { A = 1, B = 1 }` — reverse
  `1 -> B`.
- `enum_string_no_reverse`: `const enum S { On = "on" }` — values carry
  `"on"`, reverse is empty.
