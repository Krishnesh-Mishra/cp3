# `tests/cases/v0-2-5/` - the module-boundary and checker regression cases

These cases pin the behaviour a **multi-module package** needs, and the
checker guarantees the toolchain advertises. They were written from a
redis-style client implementation (`redis-cp3`) that splits its code over
several `.cp3` modules and ships the result as a `.cp3pak`.

Every case states the **desired** behaviour, not the behaviour of the day:
a case that fails today is an open defect, listed in the status table below.
A case that passes today is a regression guard - it must keep passing.

## Layout

```
tests/cases/v0-2-5/
  type-resolution-and-imports/   a name used in one module must resolve in
                                 the module that imports it
  check-vs-build/                `cp3 check` must report what `cp3 build`
                                 reports (and vice versa)
  checker-correctness/           the checker must type a program the way
                                 TypeScript does
```

Each case is the standard corpus pair - a folder with `main.cp3` (plus the
helper modules it imports) and `output.json` (the expectation). The corpus
harness (`tests/runner/run_tests.py`) discovers them with no extra wiring:

```bash
python tests/runner/run_tests.py --filter v0-2-5 --verbose --no-bench
```

## Status (2026-11-16, `cp3c` 0.2.4 + the v0.2.5 dev tree)

`ok` = passes (regression guard). `FAIL` = the desired behaviour is not
implemented yet; the case goes green when the defect is fixed.
`fixed in <sha>` = went green as part of the 2026-11-16 defect-correction
pass; kept here so the history stays readable.

### `type-resolution-and-imports/`

| case | pins | status |
| --- | --- | --- |
| `self_ref_class_field` | a class naming itself in a field annotation resolves in the importing module | ok |
| `self_ref_array_spelling` | the same through the `Array<T>` spelling | ok |
| `trailing_export_member_access` | a trailing `export { X };` exposes the class's members (property read/write, method call, function-typed field, iteration) | ok |
| `reexport_from_barrel` | `export { X } from "./x.cp3";` re-exports a usable symbol | ok |
| `imported_class_annotation` | an imported class is usable in an annotation position | ok |
| `imported_interface_annotation` | an imported interface is usable in an annotation position | ok |
| `sibling_class_parameter` | a class naming a **sibling** class of the same module in a parameter annotation resolves when the module is imported | fixed in 398a10b1 |
| `alias_before_class` | a type alias naming a class declared **later** in the same module resolves | fixed in 398a10b1 |
| `alias_after_class` | a type alias naming a class declared **earlier** in the same module resolves | fixed in 398a10b1 |
| `recursive_interface_binding` | a recursive interface can be used as a binding type | fixed in 7719ba8a |

### `check-vs-build/`

| case | pins | status |
| --- | --- | --- |
| `object_field_in_dep_check` | `cp3 check` reports a codegen error that lives in a **dependency** module (was: only the entry module's codegen was checked) | fixed in a2548cb3 |
| `object_field_in_entry_check` | `cp3 check` reports the entry module's codegen error | ok |
| `object_field_in_entry_build` | the same program fails the build with the same diagnostic | ok |
| `imported_member_write_check` | a member write on an imported class type-checks | ok |
| `imported_member_write_build` | the same program builds and runs | ok |
| `imported_module_error_reported_by_check` | `cp3 check` follows the import graph and reports the dependency's type error | ok |
| `imported_module_error_reported_by_build` | the same program fails the build with the same diagnostic | ok |

### `checker-correctness/`

| case | pins | status |
| --- | --- | --- |
| `empty_array_literal_field_initializer` | `items: number[] = [];` - the field type is the literal's context | fixed in 2c85498f |
| `empty_array_literal_assignment` | `this.items = [];` - the field type is the literal's context | fixed in 2c85498f |
| `resolver_assignment_to_member` | the `Promise.withResolvers<T>()` resolver is assignable to a `(value: T) => undefined` member | fixed in 4ddb0b13 |
| `object_to_alias_array_cast` | a cast with no C++ conversion is diagnosed by the checker instead of failing in the C++ step | fixed in 10067bbc |
| `typeof_mixed_scalar_nullish` | `typeof v === "string"` narrows `number \| string \| undefined` | fixed in 0dd511ac |
| imported_unknown_member | a member the imported class does not declare is reported (the imported class is not opaque) | ok |
| unused_import | an unused named import is reported at its specifier's span | fixed in b39f9192 |
| extends_class_member_type_mismatch | a subclass redeclares a member with an incompatible type (string for base's number) | fixed in b39f9192 |
| static_member_via_instance | a static member is rejected through an instance receiver | fixed in b39f9192 |
| duplicate_interface_declaration | the same interface name twice in one file is CP3303 | fixed in b39f9192 |
| implements_class_member_type_mismatch | a class implementing an interface declares a member of the wrong type | fixed in b39f9192 |
| implements_class_member_missing | a class implementing an interface lacks a member the interface requires | fixed in b39f9192 |
| generic_call_constraint_violation | an INFERRED type binding violates the declared extends bound at a call site | fixed in b39f9192 |
| duplicate_function_declaration | the same function parameter signature twice is a duplicate (spec §17 overload sets with different parameters stay legal) | fixed in b39f9192 |

## Notes for whoever fixes these

* A case that fails in a **dependency's** codegen can leave build artifacts
  behind; a later run in the same folder may then fail in the C++ step
  instead of with the CP diagnostic. Delete the case folder's `.cp3/`
  directory (gitignored) to get the diagnostic back. The corpus runner does
  not clean `.cp3/` between runs, but now wipes it and retries every failing
  case (see `retry_transient_failures` in tests/runner/run_tests.py), so a
  stale cache can no longer cause a permanent false red.
* `tests/cases/v0-2-2/empty_literal_cast` pins the same empty-array-literal
  defect from the v0.2.2 angle; it is green as of 2c85498f.
* The packaging half of this work (the `.cp3pak` route, the installer and the
  native-backend selection) lives in `tests/v0.2.5/package-test/`, which has
  its own runner because those cases need several commands per scenario.
