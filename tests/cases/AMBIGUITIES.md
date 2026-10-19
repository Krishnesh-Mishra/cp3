# cp3c open spec questions (`_ambiguous/` registry)

Every file below is an **open-question marker**, not a conformance assertion: it records a point the spec does not pin down. The harness (`tests/runner/run_tests.py`) discovers them, reports them in a separate `ambiguous` bucket, and never counts them as pass or fail.

This file is generated. Regenerate it with:

```bash
python tests/runner/run_tests.py --write-registry
```

**Total markers: 51** across 13 categories.

| Category | Markers |
| --- | ---: |
| `arrays-strings` | 9 |
| `async` | 1 |
| `classes` | 4 |
| `concurrency` | 6 |
| `control-flow` | 6 |
| `enums` | 1 |
| `errors` | 1 |
| `functions-closures` | 4 |
| `generics` | 2 |
| `memory-weak` | 6 |
| `modules` | 3 |
| `stdlib` | 4 |
| `types` | 4 |
| **Total** | **51** |

## `arrays-strings`

| case file | question |
| --- | --- |
| `arrays-strings/_ambiguous/array_mixed_infer.expect.json` | inferring an element type for an unannotated mixed array literal `[1, "two"]` |
| `arrays-strings/_ambiguous/array_print_format.expect.json` | `console.log` rendering of a whole array/object value (fmt representation) |
| `arrays-strings/_ambiguous/binarysearch_unsorted.expect.json` | `binarySearch` on an unsorted array: debug panic vs silently wrong result |
| `arrays-strings/_ambiguous/find_not_found.expect.json` | `find` return value when nothing matches (undefined vs other) |
| `arrays-strings/_ambiguous/forin_map_order.expect.json` | `for...in` over a `Map`: iteration order is unspecified (unordered_map) |
| `arrays-strings/_ambiguous/indexof_not_found.expect.json` | `indexOf` return value when the element is not found (-1 vs other) |
| `arrays-strings/_ambiguous/slice_negative.expect.json` | negative index arguments to `slice` (JS wraps from the end; std::substr does not) |
| `arrays-strings/_ambiguous/str_index_type.expect.json` | typed and printed form of `str[i]` (raw byte): native byte type vs number vs 1-char string |
| `arrays-strings/_ambiguous/substring_swap.expect.json` | `substring(a, b)` when a > b (JS swaps the arguments; std::substr does not) |

## `async`

| case file | question |
| --- | --- |
| `async/_ambiguous/async_arrow_function.expect.json` | is `async () => {}` (async arrow function) supported? The spec only shows `async function`. |

## `classes`

| case file | question |
| --- | --- |
| `classes/_ambiguous/getter_setter_syntax.expect.json` | Exact getter/setter declaration syntax: TS-style `get value()` (no `function` keyword, as used here) vs. cp3's `function name()` method form. The spec says only 'getters/setters (get/set)'. |
| `classes/_ambiguous/multiple_inheritance.expect.json` | How is 'multiple class inheritance is not supported' surfaced - as a parser error on `extends A, B`, or a semantic error? (A, B, C order here is illustrative.) |
| `classes/_ambiguous/static_via_instance.expect.json` | Is reading a static member through an instance (`c.x`) allowed, or must it be `C.x`? Spec maps statics to C++ but does not state the access rule. |
| `classes/_ambiguous/super_call.expect.json` | Is a `super` keyword provided to call a parent method? The spec documents single `extends` but never mentions `super`. |

## `concurrency`

| case file | question |
| --- | --- |
| `concurrency/_ambiguous/parallel_capture_diagnostic_text.expect.json` | the spec pins that a captured-let reassignment inside parallel.* is a compile error, but not the diagnostic wording/code. |
| `concurrency/_ambiguous/parallel_import_required.expect.json` | is `parallel` a global builtin (used bare in §13) or a std import? It is not listed in the std module table. |
| `concurrency/_ambiguous/parallel_range_end_exclusive.expect.json` | is the `a..b` range end exclusive? Inferred from the loop example but never stated. |
| `concurrency/_ambiguous/parallel_run_mixed_result_types.expect.json` | what is the result type of `parallel.run` when closures return different types (heterogeneous tuple vs array)? |
| `concurrency/_ambiguous/worker_call_shape.expect.json` | is `worker(...)` the called default export of an imported `.cp3` file (as the §13 example implies), and is `import worker from "x.cp3"` the canonical shape? |
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

## `enums`

| case file | question |
| --- | --- |
| `enums/_ambiguous/numeric_value_via_index.expect.json` | observing a number-backed enum's underlying numeric value via array indexing (enum-to-number conversion) |

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
| `generics/_ambiguous/generic_interface.expect.json` | Are generic interfaces (`interface Box<T>`) part of the language? The spec documents generics on classes and functions only. |
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
| `stdlib/_ambiguous/array_slice_exists.expect.json` | array.slice is not in the explicit array-method list in §09 (only string slice/substring are listed). |
| `stdlib/_ambiguous/binarysearch_return_value.expect.json` | the return value of `binarySearch(target)` (index vs boolean vs -1) is not specified. |
| `stdlib/_ambiguous/json_parse_untyped_typeof_narrow.expect.json` | whether `typeof` narrowing works directly on the `unknown` result of untyped JSON.parse is not shown. |
| `stdlib/_ambiguous/regex_test_method.expect.json` | regex literals are a core feature, but which RegExp methods (.test/.match/.exec) exist is not specified. |

## `types`

| case file | question |
| --- | --- |
| `types/_ambiguous/enum_number_print.expect.json` | What does console.log print for a number-backed enum member (underlying int 1, or the name 'Green')? The spec pins the enum->enum class mapping but not the printed form. |
| `types/_ambiguous/loose_equals_null_undefined.expect.json` | Does loose == coerce null and undefined to equal (JS gives true), or does cp3's '==' only match when the stored variant alternative is the same (giving false)? |
| `types/_ambiguous/type_alias_keyword.expect.json` | Is `type X = ...` a real language keyword? It appears only in the STDLIB pseudo-declarations, never in the core language spec's syntax. |
| `types/_ambiguous/typeof_expression.expect.json` | Is typeof allowed on an arbitrary expression, or only on a variable as the spec's wording ('typeof on an ordinary single-type variable') implies? |
