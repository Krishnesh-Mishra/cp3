# cp3c open spec questions (`_ambiguous/` registry)

Every file below is an **open-question marker**, not a conformance assertion: it records a point the spec does not pin down. The harness (`tests/runner/run_tests.py`) discovers them, reports them in a separate `ambiguous` bucket, and never counts them as pass or fail.

This file is generated. Regenerate it with:

```bash
python tests/runner/run_tests.py --write-registry
```

**Total markers: 28** across 10 categories.

| Category | Markers |
| --- | ---: |
| `arrays-strings` | 1 |
| `concurrency` | 1 |
| `control-flow` | 6 |
| `errors` | 1 |
| `functions-closures` | 4 |
| `generics` | 1 |
| `memory-weak` | 6 |
| `modules` | 3 |
| `stdlib` | 1 |
| `types` | 4 |
| **Total** | **28** |

## `arrays-strings`

| case file | question |
| --- | --- |
| `arrays-strings/_ambiguous/forin_map_order.expect.json` | `for...in` over a `Map`: iteration order is unspecified (unordered_map) |

## `concurrency`

| case file | question |
| --- | --- |
| `concurrency/_ambiguous/worker_crash_takes_process_down.expect.json` | the spec says a worker-thread crash can take the whole program down, but the observable exit code/output is not pinned. |

## `control-flow`

| case file | question |
| --- | --- |
| `control-flow/_ambiguous/break_outside_loop.expect.json` | diagnostic/behavior for `break` outside a loop or switch is not specified |
| `control-flow/_ambiguous/custom_iterable_forof.expect.json` | custom-iterable `for...of` needs `begin()`/`end()` returning `Iterator<T>`, but the `Iterator<T>` type is not otherwise specified |
| `control-flow/_ambiguous/dense_array_holes.expect.json` | how an unfilled array slot worth `null`/`undefined` is created (sparse->dense); `new Array(n)` / hole syntax is not specified |
| `control-flow/_ambiguous/forin_array_indices.expect.json` | `for...in` target set/index type: section 6 shows `for (let key in object)` over keys/indices while section 8 bans `for...in` on plain objects/class instances; whether array indices are numbers or strings is unspecified |
| `control-flow/_ambiguous/reduce_no_initial.expect.json` | `reduce` with no initial accumulator value is not specified |
| `control-flow/_ambiguous/spread_number_array_into_fixed_params.expect.json` | spreading a `number[]` into fixed-arity parameters: whether a tuple type is required is unspecified |

## `errors`

| case file | question |
| --- | --- |
| `errors/_ambiguous/finally_return_overrides.expect.json` | does a `return` inside `finally` override the try/catch return (JS semantics)? Not stated. |

## `functions-closures`

| case file | question |
| --- | --- |
| `functions-closures/_ambiguous/function_declaration_hoisting.expect.json` | Are function declarations hoisted so they may be called before their textual definition (JS behavior), or must they be defined first? |
| `functions-closures/_ambiguous/loop_closure_capture.expect.json` | Does a closure capturing a `for (let i ...)` loop variable get a fresh binding per iteration (JS: 0,1,2) or one shared cell for the whole loop (3,3,3)? Spec says let is block-scoped like JS, but the closure-capture rule says any captured reassignable let is a single shared cell. |
| `functions-closures/_ambiguous/missing_return_nonvoid.expect.json` | Is a missing `return` in a function declared to return `number` a compile error, or does it return a default value? Not addressed by the spec. |
| `functions-closures/_ambiguous/void_return_value.expect.json` | May a `void`-returning function `return` a value? The spec does not state whether this is an error. |

## `generics`

| case file | question |
| --- | --- |
| `generics/_ambiguous/infer_class_type_arg.expect.json` | Can a generic class's type argument be inferred from later usage (`new Box()` with no `<T>`), or must it always be written explicitly (`new Box<number>()` as every spec example does)? |

## `memory-weak`

| case file | question |
| --- | --- |
| `memory-weak/_ambiguous/unbroken_cycle_warning_text.expect.json` | the non-fatal cycle warning is pinned, but its exact wording/severity formatting is not. |
| `memory-weak/_ambiguous/uninitialized_field_type.expect.json` | is a class field declared without an initializer `undefined` at runtime, or default-constructed? Not specified. |
| `memory-weak/_ambiguous/weak_literal_wrapper_typing.expect.json` | how does the `{ weak: value }` object-literal wrapper (§11) type-check against a declared field? The spec example assigns it to a field inferred as `null`. |
| `memory-weak/_ambiguous/weak_modifier_optional_type.expect.json` | is the `weak` field modifier allowed to carry an explicit `\| undefined`? §11 only shows `weak parent: Parent;`. |
| `memory-weak/_ambiguous/weak_on_value_type.expect.json` | is `weak` allowed on a value-type (non-reference) field? §11 says it breaks reference cycles; behavior on primitives is not stated. |
| `memory-weak/_ambiguous/weak_read_strong_handle.expect.json` | does reading a weak field into a local take a strong handle (extending the target's lifetime)? Not specified. |

## `modules`

| case file | question |
| --- | --- |
| `modules/_ambiguous/default_export_function_decl.expect.json` | Is `export default function name(...)` a valid default-export form, or must the default be an expression/object (`export default { add }`) as the spec's examples show? |
| `modules/_ambiguous/prelude_strong_weak_order.expect.json` | When `_prelude.cp3` (strong) and `_prelude.weak.cp3` sit in the same folder, the spec fixes injection order (same, then strong, then weak) but does not pin the observable effect when both inject conflicting/different imports. |
| `modules/_ambiguous/relative_specifier_form.expect.json` | Local module specifier form: bare `base.cp3` (as the spec's own `import worker from "worker.cp3"` example) vs. ES-relative `./base.cp3`. The spec says 'plain ES module semantics' but its example is bare. |

## `stdlib`

| case file | question |
| --- | --- |
| `stdlib/_ambiguous/json_parse_untyped_typeof_narrow.expect.json` | whether `typeof` narrowing works directly on the `unknown` result of untyped JSON.parse is not shown. |

## `types`

| case file | question |
| --- | --- |
| `types/_ambiguous/enum_number_print.expect.json` | What does console.log print for a number-backed enum member (underlying int 1, or the name 'Green')? The spec pins the enum->enum class mapping but not the printed form. |
| `types/_ambiguous/loose_equals_null_undefined.expect.json` | Does loose == coerce null and undefined to equal (JS gives true), or does cp3's '==' only match when the stored variant alternative is the same (giving false)? |
| `types/_ambiguous/type_alias_keyword.expect.json` | Is `type X = ...` a real language keyword? It appears only in the STDLIB pseudo-declarations, never in the core language spec's syntax. |
| `types/_ambiguous/typeof_expression.expect.json` | Is typeof allowed on an arbitrary expression, or only on a variable as the spec's wording ('typeof on an ordinary single-type variable') implies? |
