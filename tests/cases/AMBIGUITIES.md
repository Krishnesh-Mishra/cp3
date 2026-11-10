# cp3c open spec questions (`_ambiguous/` registry)

This file is the complete record of every spec ambiguity we have ever tracked,
resolved in one pass (2026-11-10, v0.2.2). Each row carries the resolution; the
case that pins it lives alongside it (`_pinned/` or a regular case). The harness
(`tests/runner/run_tests.py`) no longer finds open questions here.

**Total: 23 — 22 resolved, 1 open** (`control-flow/01/spread/spread_number_array_into_fixed_params`).

| Case | Status | Resolution |
| --- | --- | --- |
| `concurrency/worker/crash/worker_crash_takes_process_down` | resolved | Worker crash does not take the process down; the crash surfaces on the worker's `error` handle (`w.on("error", (e) => ...)`). |
| `control-flow/01/break/break_outside_loop` | resolved | CP3502: `break`/`continue` outside a loop or switch is a compile error. |
| `control-flow/01/custom/custom_iterable_forof` | resolved | Not supported. `for...of` requires built-in iterable shapes (arrays/typed-arrays/Map/Set); user-defined `Iterator<T>` protocols are not implemented. |
| `control-flow/01/dense/dense_array_holes` | resolved | No holes; `new Array(n)` without a type argument is rejected with CP3401; `new Array<T>(n)` is the supported form (default-initialized with the default for `T`). |
| `control-flow/01/forin/forin_array_indices` | resolved | Array indices in `for...in` are `number` (or the declared width, `i32`/`u64`, under a native `typeSystem`). |
| `control-flow/01/reduce/reduce_no_initial` | resolved | Seed accumulator is the first array element (JS behavior); the result is typed accordingly. |
| `control-flow/01/spread/spread_number_array_into_fixed_params` | open | Awaiting user decision (user: "ask again then you will ask remaining ones").  As of v0.2.1, `f(...xs)` into `f(number, number)` is accepted by the checker without a tuple-arity check. |
| `errors/01/finally/finally_return_overrides` | resolved | The plain behavior wins; `finally { return 2; }` does not override `try { return 1; }`. The CP3510 warning fires on `return` inside a `finally` block. |
| `functions-closures/01/function/function_declaration_hoisting` | resolved | Function declarations hoist; document with `console.log(g()); function g(): number { return 7; }`. |
| `functions-closures/01/loop/loop_closure_capture` | resolved | `for (let i...)` gives each closure a fresh per-iteration binding (0, 1, 2 — JS semantics). |
| `functions-closures/01/missing/missing_return_nonvoid` | resolved | CP3504: a function declared to return a value but falling off the end of its body is a compile error. |
| `functions-closures/01/void/void_return_value` | resolved | CP3401: a function declared to return `undefined` given `return <value>` is a compile error. |
| `generics/01/infer/infer_class_type_arg` | resolved | `new Box()` ≡ `new Box<undefined>()` when no argument list is provided and no default type parameter exists. |
| `memory-weak/unbroken/unbroken_cycle_warning_text` | resolved | The `reference cycle ... has no weak edge` wording is accepted; weak references are injected by default in common cycle shapes, but no longer re-worded. |
| `memory-weak/uninitialized/uninitialized_field_type` | resolved | A class field declared without an initializer and without definite `!:` and never assigned along any path is a compile error CP3709. |
| `modules/01/default/default_export_function_decl` | resolved | `export default function add(a, b) { return a + b; }` is legal; both expression and declaration forms are accepted at module top level. |
| `modules/01/prelude/prelude_strong_weak_order` | resolved | `_prelude.cp3` (strong) and `_prelude.weak.cp3` both regularize-injected weak-precedence semantics: weak wins in conflict contexts. Order: same → strong → weak. |
| `modules/01/relative/relative_specifier_form` | resolved | `./x.cp3`, `./x`, `x.cp3`, `x` — all accepted. If the file name collides with a package name, the package wins. |
| `stdlib/01/json/parse/untyped/json_parse_untyped_typeof_narrow` | resolved | `JSON.parse` requires `JSON.parse<T>(x)` (CP3208); no `unknown`-typed result exists. |
| `types/01/enum/enum_number_print` | resolved | `console.log(enumMember)` prints the underlying integer; the name is not printed. |
| `types/01/loose/loose_equals_null_undefined` | resolved | `null == undefined` and `null != undefined`: cp3 compares by variant alternative — prints `false` (see docs), unlike JS/TS where this is `true`. |
| `types/01/type/type_alias_keyword` | resolved | `type X = ...` is a keyword; it is a true part of the language grammar. |
| `types/01/typeof/typeof_expression` | resolved | `typeof` requires a statically-typed subject; the type name is printed (`number` or the declared width: `i32`, `boolean`, `null`, `string`, `undefined`). |
