# cp3c open spec questions (`_ambiguous/` registry)

Every file below is an **open-question marker**, not a conformance assertion: it records a point the spec does not pin down. The harness (`tests/runner/run_tests.py`) discovers them, reports them in a separate `ambiguous` bucket, and never counts them as pass or fail.

This file is generated. Regenerate it with:

```bash
python tests/runner/run_tests.py --write-registry
```

**Total markers: 108** across 15 categories.

| Category | Markers |
| --- | ---: |
| `arrays-strings` | 9 |
| `async` | 5 |
| `classes` | 4 |
| `cli` | 23 |
| `concurrency` | 6 |
| `control-flow` | 6 |
| `enums` | 7 |
| `errors` | 7 |
| `functions-closures` | 4 |
| `generics` | 2 |
| `memory-weak` | 6 |
| `modules` | 3 |
| `operators` | 6 |
| `stdlib` | 16 |
| `types` | 4 |
| **Total** | **108** |

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
| `async/_ambiguous/await_non_promise.expect.json` | what does `await` on a non-Promise value do (JS unwraps it)? Not specified. |
| `async/_ambiguous/promise_resolve_reject.expect.json` | are the static `Promise.resolve`/`Promise.reject` present? The spec lists only all/race/allSettled. |
| `async/_ambiguous/then_chaining_return.expect.json` | whether `.then` returns a new Promise that chains (JS semantics) is not stated. |
| `async/_ambiguous/unhandled_rejection_exit_code.expect.json` | the process terminating on an unhandled rejection is pinned, but the exact exit code and printed diagnostics are not. |

## `classes`

| case file | question |
| --- | --- |
| `classes/_ambiguous/getter_setter_syntax.expect.json` | Exact getter/setter declaration syntax: TS-style `get value()` (no `function` keyword, as used here) vs. cp3's `function name()` method form. The spec says only 'getters/setters (get/set)'. |
| `classes/_ambiguous/multiple_inheritance.expect.json` | How is 'multiple class inheritance is not supported' surfaced - as a parser error on `extends A, B`, or a semantic error? (A, B, C order here is illustrative.) |
| `classes/_ambiguous/static_via_instance.expect.json` | Is reading a static member through an instance (`c.x`) allowed, or must it be `C.x`? Spec maps statics to C++ but does not state the access rule. |
| `classes/_ambiguous/super_call.expect.json` | Is a `super` keyword provided to call a parent method? The spec documents single `extends` but never mentions `super`. |

## `cli`

| case file | question |
| --- | --- |
| `cli/_ambiguous/build_debug_flag.expect.json` | the --debug build profile is a toolchain flag with no .cp3-observable semantics. |
| `cli/_ambiguous/build_release_flag.expect.json` | the --release build profile (whole-program LTO) has no observable effect inside a .cp3 program. |
| `cli/_ambiguous/clean_all_wipes_packages.expect.json` | cp3 clean --all deleting .cp3/packages/ cannot be expressed as a .cp3 case. |
| `cli/_ambiguous/clean_wipes_build.expect.json` | cp3 clean deleting .cp3/build/ cannot be expressed as a .cp3 case. |
| `cli/_ambiguous/dev_watch_mode.expect.json` | cp3 dev's watch/rebuild behavior is not expressible as a .cp3 case. |
| `cli/_ambiguous/dot_cp3_layout.expect.json` | the generated .cp3/ directory layout is a project-tooling artifact, not observable from a .cp3 program. |
| `cli/_ambiguous/exec_package_bin.expect.json` | cp3 exec resolving and running a package's bin tool is not expressible as a .cp3 case. |
| `cli/_ambiguous/fmt_formatter.expect.json` | cp3 fmt's output and the package.json "format" overrides are not expressible as a .cp3 case. |
| `cli/_ambiguous/init_generates_layout.expect.json` | the generated project layout from cp3 init is not observable through `cp3c run/check <file>`. |
| `cli/_ambiguous/init_prompts.expect.json` | cp3 init's four interactive prompts and generated files cannot be expressed as a single .cp3 case. |
| `cli/_ambiguous/install_auto_flag.expect.json` | the --auto compatibility-selection behavior is not expressible as a .cp3 case. |
| `cli/_ambiguous/install_dependencies.expect.json` | cp3 install's network/registry behavior is not expressible as a .cp3 case. |
| `cli/_ambiguous/install_github_url.expect.json` | installing from a GitHub release URL (single/multiple/none .cp3pak assets) is not expressible as a .cp3 case. |
| `cli/_ambiguous/lifecycle_postbuild_hook.expect.json` | npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program. |
| `cli/_ambiguous/lifecycle_prebuild_hook.expect.json` | npm-style pre*/post* lifecycle hooks run around CLI commands, not inside a .cp3 program. |
| `cli/_ambiguous/max_memory_flag.expect.json` | the --max-memory codegen cap is a CLI flag with no .cp3-observable semantics. |
| `cli/_ambiguous/recompile_cp3pak.expect.json` | recompiling an existing .cp3pak is a tooling operation, not a .cp3 case. |
| `cli/_ambiguous/run_vs_check.expect.json` | the distinction between `cp3 run` and `cp3 check` is a CLI-invocation property, not a property of a .cp3 program. |
| `cli/_ambiguous/spinner_rule.expect.json` | the spinner rule is a CLI UX property with no .cp3-observable semantics. |
| `cli/_ambiguous/static_link_single_binary.expect.json` | the static-link output shape is a build-artifact property, not a .cp3 case. |
| `cli/_ambiguous/toolchain_fallback.expect.json` | toolchain selection/fallback and multi-toolchain error grouping are CLI behaviors, not .cp3 cases. |
| `cli/_ambiguous/type_package_build.expect.json` | producing a .cp3pak from a package project is an artifact/tooling behavior, not a .cp3 case. |
| `cli/_ambiguous/version_flag.expect.json` | the version command's output is a CLI property, not a .cp3 case. |

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
| `enums/_ambiguous/assign_raw_number.expect.json` | assigning a raw number to a numeric-enum-typed variable (`let c: Color = 1`) |
| `enums/_ambiguous/autonumber_value.expect.json` | the underlying numeric value of an auto-incremented member after an explicit initializer is not observable through any spec-pinned operation |
| `enums/_ambiguous/compare_member_to_number.expect.json` | comparing an enum member to a raw number (Color.Red === 0) |
| `enums/_ambiguous/implicit_after_string.expect.json` | an enum member with no initializer following a string-backed member |
| `enums/_ambiguous/mixed_backing.expect.json` | an enum mixing string and number members |
| `enums/_ambiguous/numeric_value_via_index.expect.json` | observing a number-backed enum's underlying numeric value via array indexing (enum-to-number conversion) |
| `enums/_ambiguous/print_value.expect.json` | whether/how an enum value is Printable and what console.log renders for it |

## `errors`

| case file | question |
| --- | --- |
| `errors/_ambiguous/builtin_error_type.expect.json` | is there a built-in `Error` type to `throw new Error(...)`? The spec states there is no separate exception hierarchy. |
| `errors/_ambiguous/catch_binding_scope.expect.json` | is the catch binding visible after the catch block? Not specified. |
| `errors/_ambiguous/catch_without_binding.expect.json` | is `catch { }` (optional catch binding) allowed? The spec always shows `catch (e)`. |
| `errors/_ambiguous/finally_return_overrides.expect.json` | does a `return` inside `finally` override the try/catch return (JS semantics)? Not stated. |
| `errors/_ambiguous/oob_index_catchable.expect.json` | is an out-of-bounds array access a catchable thrown error or a hard runtime panic (CLI §2 calls it a runtime crash)? |
| `errors/_ambiguous/try_without_catch.expect.json` | is `try { } finally { }` (no catch clause) allowed? The spec only ever shows try/catch/finally. |
| `errors/_ambiguous/uncaught_throw_behavior.expect.json` | what is the observable exit code / output of an uncaught top-level `throw`? Only unhandled *rejections* are pinned to terminate the process. |

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

## `operators`

| case file | question |
| --- | --- |
| `operators/_ambiguous/bitwise_wide.expect.json` | bitwise operators on `number` (64-bit double): JS int32 wraparound vs C++ int64 (1 << 40) |
| `operators/_ambiguous/eq_fixed_mismatch.expect.json` | `==`/`===` between two fixed mismatched types (number vs boolean): section 3 says any two different-type values may be compared, section 5 says a mismatched-type comparison between two fixed types would not compile |
| `operators/_ambiguous/nan_print.expect.json` | stdout rendering of NaN (JS 'NaN' vs fmt 'nan') |
| `operators/_ambiguous/null_undefined_eq.expect.json` | `null == undefined` / `null === undefined`: spec keeps the two concepts separate (TS-style) but does not define cross-comparison |
| `operators/_ambiguous/unary_plus_string.expect.json` | unary `+` numeric coercion of a string operand |
| `operators/_ambiguous/unsigned_shift_negative.expect.json` | unsigned right shift `>>>` semantics for negative operands and int32 vs int64 width |

## `stdlib`

| case file | question |
| --- | --- |
| `stdlib/_ambiguous/array_slice_exists.expect.json` | array.slice is not in the explicit array-method list in §09 (only string slice/substring are listed). |
| `stdlib/_ambiguous/binarysearch_return_value.expect.json` | the return value of `binarySearch(target)` (index vs boolean vs -1) is not specified. |
| `stdlib/_ambiguous/buffer_ascii_encoding.expect.json` | encodings other than utf8/base64/hex are out of scope for beta one, but is passing "ascii" a compile error or a runtime error? |
| `stdlib/_ambiguous/console_log_array_format.expect.json` | the exact printed representation of an array or object passed to console.log is not pinned. |
| `stdlib/_ambiguous/date_getter_timezone.expect.json` | the local-vs-UTC behavior of getDate() on a Z-suffixed ISO string (which can shift the day) is not pinned. |
| `stdlib/_ambiguous/env_get_named_arg_syntax.expect.json` | does `env.get` take an options object `{ default: T }` (per the signature) or named-argument syntax `default: T` (per the example)? |
| `stdlib/_ambiguous/fetch_non_2xx_no_throw.expect.json` | fetch not throwing on 4xx/5xx is pinned, but observing it needs a live server plus a network round trip the single-file harness cannot pin deterministically. |
| `stdlib/_ambiguous/fs_readdir_order.expect.json` | the ordering of readdirSync results is not specified. |
| `stdlib/_ambiguous/json_parse_untyped_typeof_narrow.expect.json` | whether `typeof` narrowing works directly on the `unknown` result of untyped JSON.parse is not shown. |
| `stdlib/_ambiguous/json_stringify_indent_format.expect.json` | the exact whitespace/indentation produced by `JSON.stringify(value, indent)` is not pinned. |
| `stdlib/_ambiguous/map_iteration_order.expect.json` | Map/Set wrap unordered_map/unordered_set, so iteration order is unspecified. |
| `stdlib/_ambiguous/math_round_negative_half.expect.json` | Math is "Node/JS-style" but JS rounds -2.5 to -2 while C++ round() gives -3; the negative-half case is not pinned. |
| `stdlib/_ambiguous/network_headers_object_vs_map.expect.json` | RequestInit.headers is typed `Map<string,string>` in the signature but shown as a plain object in the example. |
| `stdlib/_ambiguous/parseint_hex.expect.json` | parseInt is backed by std::strtol; whether a 0x prefix is honored (base detection) is not pinned. |
| `stdlib/_ambiguous/regex_test_method.expect.json` | regex literals are a core feature, but which RegExp methods (.test/.match/.exec) exist is not specified. |
| `stdlib/_ambiguous/string_conversion_global.expect.json` | the cheat sheet shows `String(x)`, but it is not listed among the core-language builtins in §03/§20. |

## `types`

| case file | question |
| --- | --- |
| `types/_ambiguous/enum_number_print.expect.json` | What does console.log print for a number-backed enum member (underlying int 1, or the name 'Green')? The spec pins the enum->enum class mapping but not the printed form. |
| `types/_ambiguous/loose_equals_null_undefined.expect.json` | Does loose == coerce null and undefined to equal (JS gives true), or does cp3's '==' only match when the stored variant alternative is the same (giving false)? |
| `types/_ambiguous/type_alias_keyword.expect.json` | Is `type X = ...` a real language keyword? It appears only in the STDLIB pseudo-declarations, never in the core language spec's syntax. |
| `types/_ambiguous/typeof_expression.expect.json` | Is typeof allowed on an arbitrary expression, or only on a variable as the spec's wording ('typeof on an ordinary single-type variable') implies? |
