# cp3c open spec questions (`_ambiguous/` registry)

Every file below is an **open-question marker**, not a conformance assertion: it records a point the spec does not pin down. The harness (`tests/runner/run_tests.py`) discovers them, reports them in a separate `ambiguous` bucket, and never counts them as pass or fail.

This file is generated. Regenerate it with:

```bash
python tests/runner/run_tests.py --write-registry
```

**Total markers: 23** across 23 categories.

| Category | Markers |
| --- | ---: |
| `concurrency/worker/crash/worker_crash_takes_process_down` | 1 |
| `control-flow/01/break/break_outside_loop` | 1 |
| `control-flow/01/custom/custom_iterable_forof` | 1 |
| `control-flow/01/dense/dense_array_holes` | 1 |
| `control-flow/01/forin/forin_array_indices` | 1 |
| `control-flow/01/reduce/reduce_no_initial` | 1 |
| `control-flow/01/spread/spread_number_array_into_fixed_params` | 1 |
| `errors/01/finally/finally_return_overrides` | 1 |
| `functions-closures/01/function/function_declaration_hoisting` | 1 |
| `functions-closures/01/loop/loop_closure_capture` | 1 |
| `functions-closures/01/missing/missing_return_nonvoid` | 1 |
| `functions-closures/01/void/void_return_value` | 1 |
| `generics/01/infer/infer_class_type_arg` | 1 |
| `memory-weak/unbroken/unbroken_cycle_warning_text` | 1 |
| `memory-weak/uninitialized/uninitialized_field_type` | 1 |
| `modules/01/default/default_export_function_decl` | 1 |
| `modules/01/prelude/prelude_strong_weak_order` | 1 |
| `modules/01/relative/relative_specifier_form` | 1 |
| `stdlib/01/json/parse/untyped/json_parse_untyped_typeof_narrow` | 1 |
| `types/01/enum/enum_number_print` | 1 |
| `types/01/loose/loose_equals_null_undefined` | 1 |
| `types/01/type/type_alias_keyword` | 1 |
| `types/01/typeof/typeof_expression` | 1 |
| **Total** | **23** |

## `concurrency/worker/crash/worker_crash_takes_process_down`

| case file | question |
| --- | --- |
| `concurrency/_ambiguous/worker/crash/worker_crash_takes_process_down/output.json` | the spec says a worker-thread crash can take the whole program down, but the observable exit code/output is not pinned. |

## `control-flow/01/break/break_outside_loop`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/break/break_outside_loop/output.json` | diagnostic/behavior for `break` outside a loop or switch is not specified |

## `control-flow/01/custom/custom_iterable_forof`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/custom/custom_iterable_forof/output.json` | custom-iterable `for...of` needs `begin()`/`end()` returning `Iterator<T>`, but the `Iterator<T>` type is not otherwise specified |

## `control-flow/01/dense/dense_array_holes`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/dense/dense_array_holes/output.json` | how an unfilled array slot worth `null`/`undefined` is created (sparse->dense); `new Array(n)` / hole syntax is not specified |

## `control-flow/01/forin/forin_array_indices`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/forin/forin_array_indices/output.json` | `for...in` target set/index type: section 6 shows `for (let key in object)` over keys/indices while section 8 bans `for...in` on plain objects/class instances; whether array indices are numbers or strings is unspecified |

## `control-flow/01/reduce/reduce_no_initial`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/reduce/reduce_no_initial/output.json` | `reduce` with no initial accumulator value is not specified |

## `control-flow/01/spread/spread_number_array_into_fixed_params`

| case file | question |
| --- | --- |
| `control-flow/01/_ambiguous/spread/spread_number_array_into_fixed_params/output.json` | spreading a `number[]` into fixed-arity parameters: whether a tuple type is required is unspecified |

## `errors/01/finally/finally_return_overrides`

| case file | question |
| --- | --- |
| `errors/01/_ambiguous/finally/finally_return_overrides/output.json` | does a `return` inside `finally` override the try/catch return (JS semantics)? Not stated. |

## `functions-closures/01/function/function_declaration_hoisting`

| case file | question |
| --- | --- |
| `functions-closures/01/_ambiguous/function/function_declaration_hoisting/output.json` | Are function declarations hoisted so they may be called before their textual definition (JS behavior), or must they be defined first? |

## `functions-closures/01/loop/loop_closure_capture`

| case file | question |
| --- | --- |
| `functions-closures/01/_ambiguous/loop/loop_closure_capture/output.json` | Does a closure capturing a `for (let i ...)` loop variable get a fresh binding per iteration (JS: 0,1,2) or one shared cell for the whole loop (3,3,3)? Spec says let is block-scoped like JS, but the closure-capture rule says any captured reassignable let is a single shared cell. |

## `functions-closures/01/missing/missing_return_nonvoid`

| case file | question |
| --- | --- |
| `functions-closures/01/_ambiguous/missing/missing_return_nonvoid/output.json` | Is a missing `return` in a function declared to return `number` a compile error, or does it return a default value? Not addressed by the spec. |

## `functions-closures/01/void/void_return_value`

| case file | question |
| --- | --- |
| `functions-closures/01/_ambiguous/void/void_return_value/output.json` | May a `void`-returning function `return` a value? The spec does not state whether this is an error. |

## `generics/01/infer/infer_class_type_arg`

| case file | question |
| --- | --- |
| `generics/01/_ambiguous/infer/infer_class_type_arg/output.json` | Can a generic class's type argument be inferred from later usage (`new Box()` with no `<T>`), or must it always be written explicitly (`new Box<number>()` as every spec example does)? |

## `memory-weak/unbroken/unbroken_cycle_warning_text`

| case file | question |
| --- | --- |
| `memory-weak/_ambiguous/unbroken/unbroken_cycle_warning_text/output.json` | the non-fatal cycle warning is pinned, but its exact wording/severity formatting is not. |

## `memory-weak/uninitialized/uninitialized_field_type`

| case file | question |
| --- | --- |
| `memory-weak/_ambiguous/uninitialized/uninitialized_field_type/output.json` | is a class field declared without an initializer `undefined` at runtime, or default-constructed? Not specified. |

## `modules/01/default/default_export_function_decl`

| case file | question |
| --- | --- |
| `modules/01/_ambiguous/default/default_export_function_decl/output.json` | Is `export default function name(...)` a valid default-export form, or must the default be an expression/object (`export default { add }`) as the spec's examples show? |

## `modules/01/prelude/prelude_strong_weak_order`

| case file | question |
| --- | --- |
| `modules/01/_ambiguous/prelude/prelude_strong_weak_order/output.json` | When `_prelude.cp3` (strong) and `_prelude.weak.cp3` sit in the same folder, the spec fixes injection order (same, then strong, then weak) but does not pin the observable effect when both inject conflicting/different imports. |

## `modules/01/relative/relative_specifier_form`

| case file | question |
| --- | --- |
| `modules/01/_ambiguous/relative/relative_specifier_form/output.json` | Local module specifier form: bare `base.cp3` (as the spec's own `import worker from "worker.cp3"` example) vs. ES-relative `./base.cp3`. The spec says 'plain ES module semantics' but its example is bare. |

## `stdlib/01/json/parse/untyped/json_parse_untyped_typeof_narrow`

| case file | question |
| --- | --- |
| `stdlib/01/_ambiguous/json/parse/untyped/json_parse_untyped_typeof_narrow/output.json` | whether `typeof` narrowing works directly on the `unknown` result of untyped JSON.parse is not shown. |

## `types/01/enum/enum_number_print`

| case file | question |
| --- | --- |
| `types/01/_ambiguous/enum/enum_number_print/output.json` | What does console.log print for a number-backed enum member (underlying int 1, or the name 'Green')? The spec pins the enum->enum class mapping but not the printed form. |

## `types/01/loose/loose_equals_null_undefined`

| case file | question |
| --- | --- |
| `types/01/_ambiguous/loose/loose_equals_null_undefined/output.json` | Does loose == coerce null and undefined to equal (JS gives true), or does cp3's '==' only match when the stored variant alternative is the same (giving false)? |

## `types/01/type/type_alias_keyword`

| case file | question |
| --- | --- |
| `types/01/_ambiguous/type/type_alias_keyword/output.json` | Is `type X = ...` a real language keyword? It appears only in the STDLIB pseudo-declarations, never in the core language spec's syntax. |

## `types/01/typeof/typeof_expression`

| case file | question |
| --- | --- |
| `types/01/_ambiguous/typeof/typeof_expression/output.json` | Is typeof allowed on an arbitrary expression, or only on a variable as the spec's wording ('typeof on an ordinary single-type variable') implies? |
