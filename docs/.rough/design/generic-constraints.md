# Generic constraints and defaults `<T extends X = Y>`

Status: **consumer activated in sema** (`resolveNamedType`, the single
choke point for every type-annotation position). ROADMAP L row
"Generic constraints and defaults: `<T extends X = Y>`" (Planned).
No new diagnostic code, no runtime metadata, no codegen change.

This document is the enforcement contract. The design model itself
(`GenericParamSpec`) already existed in
`src/common/include/cp3/core/utility_types.hpp`; the earlier gap note
is `utility-types.md` §3/§6b.

## 1. Syntax (parser, already landed, verified)

`parseTypeParamsIfPresent` (`src/parser/src/decl/declarations.cpp`)
parses, for functions, classes, interfaces, aliases, arrows, methods
and function types:

```cp3
<T extends X = Y>   // constraint + default
<T extends X>       // constraint only
<T = Y>             // default only
<T>                 // unconstrained
```

`extends` precedes `=`; each of `X`/`Y` is a full `parseType()`, so
nested generics (`<T extends Box<number>>`), unions, tuples, object
and function types parse, and nested `>>` is handled by
`expectGreater` (`pendingGT_`). Both children land on the
`TypeParameter` AST node (`constraint` / `defaultType`,
`cp3/core/ast.hpp`) and both receive node ids
(`node_ids.cpp`), so sema can resolve them.

## 2. The model

`GenericParamSpec` (`utility_types.hpp`) is the resolved form of one
`TypeParameter`: `name`, `constraint` + `hasConstraint`,
`defaultType` + `hasDefault`. The encoding contract between the
producer (declaration resolution) and the consumer (instantiation
checking) is the resolved `TypeKind::TypeParam`'s `parts`:

| Slot | Meaning |
|---|---|
| `parts[0]` (when present) | resolved `extends` constraint — the pre-existing convention; generic *function calls* already read it (`generic_check.cpp`) |
| `parts[1]` (when present) | resolved `= Y` default — new slot, defined by this patch |

`parts[1]` is read only by the consumer below; no other pass walks a
`TypeParam` expecting exactly one part (`probeBase`, the utility-type
transform and `generic_check.cpp` all use `parts.front()`).

## 3. Enforcement (this patch, `src/sema/src/checks/type/type_check.cpp`)

All three instantiation sites in `resolveNamedType` run the same two
steps, in order: **arity** (existing, CP3418), then **constraints +
default-fill** (new), then the existing substitution.

For each site the specs are recovered by `collectSpecs(names, bodies)`:
it walks the declaration's already-resolved body — the alias body, the
interface member types, or the class member types plus method and
constructor signature types — and builds one `GenericParamSpec` per
parameter name from the `TypeParam` leaves it finds (`parts[0]` /
`parts[1]` as above). `checkInstantiation(kind, name, specs, effArgs)`
then:

1. **Fills defaults.** A parameter with no provided argument takes its
   default; a default may mention earlier parameters
   (`<T, U extends T = T>`), so earlier arguments are substituted into
   it (`substituteType`) before use. The filled list is what gets
   substituted into the body (alias/interface) or carried in `parts`
   (class), so a bare `Box` for `class Box<T = number>` instantiates
   as `Box<number>`. A missing argument with no default keeps the
   pre-existing permissive bare-parameter behaviour.
2. **Checks provided arguments.** Each *provided* argument is compared
   against its constraint with `wouldAccept(arg, constraint, false)`
   (the same predicate generic function calls use), after substituting
   the earlier provided arguments into the constraint. A violation is
   **CP3412** (`kConstraintNotSatisfied`), and the instantiation
   resolves to `unknown` (fail-closed, mirroring the excess-arity
   legs; no cascaded CP3401 from a half-substituted body).
3. Only provided arguments are checked. A default that violates its
   own constraint is a declaration-site error (the producer's job,
   §5); reporting it at every use site would anchor the diagnostic at
   the wrong span.

Message sketch (constraint spelled with its declared name when a named
constraint was registered, via `typeParamConstraintNames_`, exactly as
generic function calls do):

```
type argument 'number' does not satisfy the constraint 'Shape' of type parameter 'T' in type alias 'Box'
```

## 4. Diagnostics

| Situation | Code | Where |
|---|---|---|
| Provided type argument violates `extends` (alias / interface / class) | **CP3412** `kConstraintNotSatisfied` | `resolveNamedType` (this patch) |
| Excess type arguments | CP3418 `kInvalidTypeArgument` | unchanged, runs first |
| Explicit type argument violates a generic *function*'s constraint | CP3401 `kTypeMismatch` | `generic_check.cpp` — **already enforced**, left untouched (see §6.5) |
| Missing type argument with no default | none (permissive bare `TypeParam`) | unchanged |
| Constraint/default naming an undefined type | existing CP3302 at declaration *if* the producer resolves it (§5) | producer |

## 5. Wiring: producer contract (lead, outside this patch's file set)

The consumer activates the moment the resolved `TypeParam` leaves
carry `parts[0]`/`parts[1]`. Today the declaration passes bind bare
type parameters — `enterClassTypeParams` (`checks/class/class_check.cpp`)
for aliases, interfaces and classes, and only `registerTypeParams`
(`support/scopes.cpp`, functions) writes `parts[0]`. To close the
gap the producer must, at declaration time in `declareTypeAlias`
(`support/declarations.cpp`), `registerInterface` and `resolveClass`:

1. Resolve each `TypeParameter`'s `constraint` and `defaultType`
   once (the same way `registerTypeParams` already resolves a
   function's constraint, including registering the named spelling in
   `typeParamConstraintNames_`).
2. Bind the parameter name to a `TypeParam` carrying
   `parts[0] = constraint` (when present) and `parts[1] = default`
   (when present), so every `TypeParam` leaf in the resolved body
   carries its spec.
3. Validate at the declaration span (CP3412): the default must
   satisfy its own constraint, and a constraint must not be the
   parameter itself. Do not validate defaults at use sites (§3.3).
4. Optionally (cleaner, but needs `detail/analyzer.h` + the three
   declaration files): store `std::vector<GenericParamSpec>` beside
   `typeAliasParams_` / `GenericInterface` / `ClassInfo` and read it
   in `resolveNamedType` instead of recovering specs from body
   leaves. The consumer's check is written against the spec shape,
   so swapping the source of the specs is a local change.

Generic *function* defaults need the same fill in `inferGenericCall`
(`checks/call/generic_check.cpp`): a parameter bound by neither an
explicit argument nor inference takes its default instead of staying
a bare `TypeParam`. That file is outside this patch's set.

## 6. Limitations (explicit)

1. **Producer encoding.** Until §5 lands, alias/interface/class
   `TypeParam` leaves are bare, so recovered specs are unconstrained
   and the CP3412 leg is dormant for those declarations — nothing
   regresses, and nothing is falsely diagnosed. The consumer,
   diagnostics and default-fill activate in full the moment the
   producer writes `parts[0]`/`parts[1]` (§5.2); they are exercised
   against specs built from encoded leaves.
2. **Parameters unused in the body.** `collectSpecs` recovers a spec
   only from a `TypeParam` leaf in the body. `type A<T extends X> =
   number` never mentions `T`, so `A<string>` is unchecked until the
   producer stores specs beside the name lists (§5.4). Used
   parameters — the overwhelmingly common case — are covered.
3. **Type-parameter arguments are permissive.** `wouldAccept`
   accepts any `TypeParam` source unconditionally, so passing an
   unconstrained `U` where `T extends Shape` is expected is not
   diagnosed (same behaviour as generic function calls). Checking
   `U`'s own constraint against the target constraint is a separate
   change to `wouldAccept` and would need care around the existing
   permissive contract.
4. **Self-referential constraints** (`T extends Box<T>`, `T extends T`)
   are skipped (permissive) — `mentionsParam` guards the check.
5. **Function calls report CP3401, not CP3412.** `generic_check.cpp`
   already enforces constraints for explicit type arguments but with
   `kTypeMismatch`. Unifying that site onto CP3412 is a one-line
   change in `checks/call/` (outside this patch's file set) and is
   deliberately left to the call-check owner; until then the two
   sites disagree on the code for the same violation.
6. **`new Box<wrong>()`.** Constraint checking for constructor type
   arguments runs through the `new`-expression checker
   (`checks/class/class_conform_check.cpp`, outside this patch's
   set), not through `resolveNamedType`; if that path resolves the
   class name as a `NamedType` it benefits automatically, otherwise
   the annotation path (`let b: Box<wrong>`) is the enforced one
   and `new` needs its own leg.
7. **Missing arguments stay permissive.** TypeScript errors on
   `Box` for `class Box<T>`; CP3's existing rule (bare type
   parameter) is preserved for non-defaulted parameters, so this
   patch can only *fill* defaults, never start rejecting bare uses.
8. No codegen change: constraints and defaults are compile-time
   only; the substituted/filled types are ordinary `Type`s, so
   codegen sees nothing new (Rule 0: no runtime metadata).

## 7. Examples

```cp3
interface Shape { area: number }
interface Box<T extends Shape = Shape> { value: T }
type Alias<T extends Shape> = { value: T }
class Items<T extends string = string> { first: T }

let a: Box<Shape>;            // ok
let b: Box<number>;           // CP3412: 'number' does not satisfy 'Shape' (T)
let c: Alias<{ area: 1 }>;    // ok (object shape conforms to Shape)
let d: Items;                 // ok: T filled with default `string`
let e: Items<number>;         // CP3412: 'number' does not satisfy 'string' (T)
```

## 8. Tests to add (not run in this patch)

Sema doc-test cases (`tests/cases`), asserting codes only:

- `generic_constraint_alias`: `type A<T extends Shape> = { v: T }` —
  `A<Shape>` accepted, `A<number>` → CP3412.
- `generic_constraint_interface`: `interface B<T extends Shape> {
  v: T }` — same pair.
- `generic_constraint_class`: `class C<T extends Shape> { v: T }` —
  `let x: C<number>` → CP3412; `let y: C<Shape>` accepted.
- `generic_default_fill`: `class D<T = number> { v: T }` — `let x: D`
  accepts `{ v: 1 }` shaped use / `D` defaults to `number`;
  `type E<T = number> = T[]` — `E` is `number[]`.
- `generic_default_violates`: `class F<T extends Shape = number>`
  — CP3412 at the declaration (producer, §5.3).
- `generic_constraint_earlier_param`: `<T, U extends T>` — `G<number,
  string>` → CP3412, `G<number, number>` accepted.
- `generic_constraint_permissive`: unknown/error arguments do not
  cascade CP3412.
