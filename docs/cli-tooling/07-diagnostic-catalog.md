# 7. Diagnostic catalog

Every problem cp3 finds while compiling, checking or running a program carries a stable code - `CP3xxx` - so you can look it up here, assert on it in tests, and search it on the web. The codes themselves are defined once in [`src/common/include/cp3/diagnostic_codes.hpp`](../../src/common/include/cp3/diagnostic_codes.hpp); codes are grouped by the stage that owns them and are never renumbered after release.

This catalog lists every code that exists today: the codes the compiler actually reports, and the codes reserved inside each range for features that are still landing (marked as **reserved** - they are never emitted yet, so you will not see them in output).

`python tools/check_diagnostics.py` keeps this honest: it fails if a header code has no entry below, or if `src/**` mentions a code the header does not define.

## How a diagnostic reads

Every problem prints one stable, machine-parseable line:

```
<path>:<line>:<col>: <severity>: <code>: <message>
```

For example:

```
src/main.cp3:3:1: error: CP3305: 'console' has no member 'logh'; did you mean 'log'?
```

`<severity>` is `error`, `warning` or `note`. Notes carry the code of the diagnostic they explain and print as their own machine line.

On an interactive terminal - or with `--verbose` - each diagnostic is followed by a pretty block: the source excerpt, a caret under the offending span, and one `help:` line per suggestion:

```
error[CP3305]: 'console' has no member 'logh'; did you mean 'log'?
  --> src/main.cp3:3:1
    |
  3 | console.logh("hi");
    | ^
  help: Did you mean 'log'?
```

Exit codes:

* `0` - no diagnostics.
* `1` - at least one error.
* `3` - every error is `CP3002` ("not implemented in this build yet"). The CLI prints a marker line containing `not implemented`, and `tests/runner/run_tests.py` reports the case as **skipped**, never failed, so scaffolding cases do not pollute the failure count.

## "Did you mean...?" suggestions

Name-resolution failures never dead-end when a close match exists in scope:

* **A case-only difference always ranks first.** `Console` against `console` is the top hit, not a distance tie.
* **Otherwise it is edit distance per user-visible character** (not per byte), with a small prefix/suffix bonus, and a tight budget: one typo for short names, a little more for long ones (`1 + length / 3`). Below the budget there is no suggestion - the compiler never guesses.
* **At most three candidates**, best first. The single best one is folded into the message itself (`...; did you mean 'log'?`) so it survives even where pretty formatting is off.
* In the pretty block the full ranked list is repeated as a `help:` line: `Did you mean 'log'?` for one candidate, `Did you mean 'a' or 'b'?` for two, `Did you mean 'a', 'b', or 'c'?` for three.
* **A missing standard-module import gets the exact statement**: ``cannot find name 'Math'; add `import Math from "std/math";` ``, with the same text as the `help:` line.
* **An import-form mistake gets the working form**: importing a named binding from a default-only module (or the reverse) reports the correct spellings; a misspelled export gets ranked candidates plus `named exports: 'a', 'b', or 'c'`.

Candidates come from what is actually visible at the reference: names in scope, the implemented member list of a standard namespace (Math, path, os, crypto, console, JSON) or of a typed receiver's shape, and the export surface of the module being imported. Where a family's shape is still open (for example `network`, `process`), member misses stay pending (`CP3002`) instead of guessing.

## Internal (3001-3009)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3002 | `kNotImplemented` | A recognized construct whose implementation has not landed in this build reached the compiler: codegen for an out-of-scope AST form, sema for a pending language part, or the CLI for a whole command. It is a "not built yet" signal, not a bug in the program. | `not implemented yet: this statement` | Rewrite with a supported equivalent if you must ship now; otherwise treat the build as pending - a program whose only errors are CP3002 exits 3 and the test runner counts it as skipped. |

## Lexical (3100-3199)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3101 | `kInvalidCharacter` | The scanner met a byte that cannot begin any token. Also reported for a `\` outside a string or template literal. | `unexpected character '#'` | Remove the character, escape it (`\` inside a string is written `\\`), or put the text in a string literal. |
| CP3102 | `kUnterminatedString` | A `"` or `'` string reached a line break or the end of the file before its closing quote. | `unterminated string literal` | Close the quote on the same line, write `\n` for a newline, or use a template literal for multi-line text. |
| CP3103 | `kUnterminatedTemplate` | A backtick template literal, or a `${` substitution inside it, never closed. | `unterminated template literal` | Close the substitution with `}` and the template with a closing backtick. |
| CP3104 | `kUnterminatedComment` | A `/*` block comment was never closed. | `unterminated block comment` | Add the closing `*/`. |
| CP3105 | `kInvalidNumericLiteral` | A numeric literal is malformed: a digit does not match its radix prefix, or trailing characters follow the number. | `invalid numeric literal` | Match digits to the prefix (`0b101`, `0o17`, `0xFF`) and remove trailing characters. |
| CP3106 | `kInvalidEscape` | A `\` escape in a string or template literal is not a valid cp3 escape. Legacy octal escapes are rejected on purpose. | `invalid escape sequence` | Use a supported escape: `\n`, `\t`, `\\`, `\"`, `\xHH`, `\uHHHH`, `\u{...}`. Delete legacy octal forms. |
| CP3107 | `kInvalidRegExp` | A regular expression literal is malformed: never closed, uses an unsupported flag, or repeats a flag. | `unterminated regular expression literal` | Close the pattern with `/`; use only supported flags, each at most once. |

## Syntax (3200-3299)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3201 | `kUnexpectedToken` | The parser expected a different piece of syntax; the message names exactly what was expected. Also used for constructs the parser knowingly rejects (generators, decorators, index signatures, multiple inheritance) with a short explanation. | `expected an expression` | Supply the named syntax. For a rejected construct, use the supported alternative the message points to. |
| CP3202 | `kExpectedToken` | A mandatory keyword or punctuation token is missing; the message names it. | `expected '(' after 'while'` | Add the named token. |
| CP3204 | `kInvalidAssignmentTarget` | The left side of an assignment (or update) is not something a value can be written into. | `invalid assignment target` | Assign to a name, a property access (`obj.field`) or an index (`arr[i]`), not to an expression such as `a + b` or a call result. |
| CP3206 | `kExpectedDeclaration` | The parser wanted a declaration and found something else. | `expected a declaration after 'export'` | Export a declaration (`export function f() {}`, `export const x = 1;`) or use `export { name };`. |
| CP3207 | `kMissingSemicolon` | A statement is not terminated with the required semicolon. | `expected ';'` | Add the `;`. |

## Name resolution (3300-3399)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3301 | `kUndefinedIdentifier` | A name does not resolve to any visible declaration, builtin or imported binding. A close visible name is folded into the message; a standard module that was simply not imported yet gets the exact import statement instead. | `cannot find name 'massage'; did you mean 'message'?` | Fix the typo, declare the binding, or add the import the message hands you. |
| CP3302 | `kUndefinedType` | A type annotation names a type that does not exist in scope. | `cannot find type name 'Strng'` | Declare or import the type; check the spelling. |
| CP3303 | `kDuplicateDeclaration` | The same name is declared twice in one scope. | `duplicate declaration of 'value'` | Rename one binding, remove the duplicate, or reuse the first declaration. |
| CP3304 | `kUnresolvedImport` | A named import asks for a member the module does not export. The message lists the closest real exports; for a default-only module (for example `std/console`) it hands over the default-import form instead. | ``module 'std/console' does not export 'console'; use the default import: `import console from "std/console";` `` | Use the suggested import form, or import one of the listed exports. |
| CP3305 | `kMemberNotFound` | A member access on a known receiver names a member that does not exist. Receivers include standard namespaces (Math, console, path, os, crypto, JSON) and typed values whose shape is known. The best candidate is folded into the message. | `'console' has no member 'logh'; did you mean 'log'?` | Fix the typo or use a listed member. For an object, check the declared shape; for a standard namespace, check the module's member table. |
| CP3306 | `kUndefinedLabel` | `break label;` or `continue label;` names a label that no enclosing labeled loop or switch defines. | `undefined label 'outer'` | Add the label to the enclosing loop or switch, or drop it from the statement. |

## Types (3400-3499)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3401 | `kTypeMismatch` | A general type mismatch: an assignment, initializer, argument or operation carries a value the target cannot hold. The message names the concrete rule (assign-to, property, array or tuple, typed-array constructor, literal inference). | `type mismatch: cannot assign 'string' to 'number'` | Convert the value, widen the target type, or correct the annotation so both sides agree. |
| CP3402 | `kNotAssignable` | A value is used where none can go: the reported case is returning a value from a `void` function. | `a void function cannot return a value` | Give the function a return type and return the value, or drop the `return` expression. |
| CP3403 | `kArityMismatch` | A call passes the wrong number of arguments for the callee's signature. | `arity mismatch: expected 2, got 1 argument` | Pass the expected count, or make parameters optional in the signature if the shorter call is intended. |
| CP3404 | `kNotCallable` | The callee expression resolves to a value that is not a function. | `value of type 'number' is not callable` | Call a function; check for a missing property access or a shadowed binding. |
| CP3406 | `kInvalidOperandTypes` | A unary or comparison operator got an operand type it does not accept. | `invalid operand type 'string' for unary '-'` | Convert the operand to the type the operator needs; comparisons need both sides compatible. |
| CP3407 | `kUnknownTypeArgument` | A raw C pointer or reference type appears where only a cp3 type argument is allowed. | `raw pointer/reference types are not permitted here` | Use a wrapper or value type; raw pointers belong behind native bindings, not ordinary type positions. |
| CP3408 | `kConstReassignment` | Assignment targets a binding declared `const`. | `cannot reassign to const 'total'` | Declare it `let` if it must change, or assign to a different variable. |
| CP3409 | `kExcessProperty` | An object literal supplies a property the target type does not declare. | `excess property 'z' is not part of the target type` | Remove the property, add it to the target type, or annotate the literal with a type that allows it. |
| CP3410 | `kMissingProperty` | An object literal is missing a property the target type requires. | `missing required property 'name' in object literal` | Add the property, or make it optional in the target type. |
| CP3411 | `kMixedTypeArithmetic` | `+` mixed number and string, or an arithmetic operator got a non-number operand. cp3 never coerces between the two. | `type mismatch: mixed-type arithmetic with '+' on 'string' and 'number' (only number+number or string+string is allowed)` | Convert explicitly (`String(n)`, `Number(s)`), or keep both operands the same type. |
| CP3414 | `kUnknownType` | An `unknown` value is used before it is narrowed: assigned, read as a property, or indexed. | `value of type 'unknown' is not assignable to 'number' without a type check` | Narrow first with a runtime check (`typeof`, a discriminant test), then use the narrowed value. |
| CP3415 | `kInvalidCondition` | A condition position received a `void` expression (a call that returns nothing). | `invalid condition: a void expression is not a valid condition` | Test an actual value, or change the callee to return one. |
| CP3416 | `kNoMatchingOverload` | A call matches none of the overloads of a function or member: the arity fits, but the argument types do not. | `no overload of 'map' matches the supplied arguments` | Adjust the arguments to match one overload; the member's docs table lists them. |

## Statements and control flow (3500-3599)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3501 | `kReturnOutsideFunction` | A `return` sits at module top level or in another non-function position. | `return outside of a function` | Move it into a function, or remove it. |
| CP3502 | `kBreakOutsideLoop` | A `break` is not inside a loop or switch. | `break outside of a loop or switch` | Move the `break` inside a loop or switch, or remove it. |
| CP3503 | `kContinueOutsideLoop` | A `continue` is not inside a loop. | `continue outside of a loop` | Move the `continue` inside a loop, or remove it. |
| CP3504 | `kMissingReturn` | A function declared to return a value can finish without returning one. | `function 'f' is declared to return a value but may fall off the end of its body` | Return a value on every path, or change the return type to `void`. |

## Modules and imports (3600-3699)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3601 | `kModuleNotFound` | An import specifier resolves to no local file, installed package or standard module (or exists but cannot be read). | `cannot find module 'std/mathx'` | Check the specifier, the path, or the package; standard modules have a fixed, documented name list. |
| CP3604 | `kMissingDefaultExport` | `import x from "..."` was used where the module has no default export; only named exports exist. | ``module 'std/collections' has no default export; named exports: 'Map' or 'Set'`` | Use a named import (``import { Map } from "std/collections";``), or import the default of a default-only module. |
| CP3605 | `kPreludeError` | An internal support file shipped with the toolchain could not be read; usually a damaged install or cache. | `cannot read prelude file '...'` | Repair or reinstall the toolchain; report it if it persists. |

## Banned JS patterns (3800-3899)

These codes enforce the deliberate "no JavaScript footguns" rules; each message explains the cp3 alternative.

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3801 | `kDeleteBanned` | The JS `delete` operator is banned: a fixed-shape object has no removable fields. | `the 'delete' operator is banned; a fixed-shape object has no removable fields (use Map<K, V>.delete)` | Use `Map<K, V>.delete(key)`, or model absence with an optional field. |
| CP3803 | `kIndexSignatureBanned` | Index signatures and index access on plain objects are banned: a plain object's keys are fixed at compile time. | `index access on a plain object is not supported; its keys are fixed at compile time (use Map<K, V> for dynamic keys)` | Use `Map<K, V>` for dynamic keys, or access fields by name. |
| CP3804 | `kArgumentsBanned` | The JS `arguments` object is banned. | `the 'arguments' object is banned; use a rest parameter instead` | Declare a rest parameter (`...rest: T[]`). |
| CP3805 | `kForInPlainObjectBanned` | `for...in` over a plain object is banned: its keys are known at compile time. | `for...in over a plain object is banned (its keys are known at compile time); use Map<K, V> or access fields by name` | Iterate a `Map<K, V>`, or access the fields by name. |
| CP3806 | `kAnyTypeBanned` | The `any` type is banned: it disables checking and hides real mistakes. | `the 'any' type is banned; use 'unknown' or a concrete type` | Use `unknown` plus a narrowing check, or the concrete type. |

## Concurrency (3900-3999)

| Code | Name | Meaning | Example message | Fix |
| ---- | ---- | ------- | --------------- | --- |
| CP3901 | `kParallelSharedMutation` | Inside a `parallel.*` closure, an indexed write to captured state cannot be proven disjoint from other iterations, so it could race. Only `parallel.for`'s own induction variable may index a write. | `unsafe indexed write to captured state inside a parallel.* closure: only parallel.for's own induction variable is allowed as the index (a computed index cannot be proven disjoint)` | Index with the induction variable only, or return values and combine them on the calling thread. |
| CP3902 | `kParallelCaptureReassign` | A captured variable is reassigned or mutated inside a `parallel.*` closure; captures are shared, non-owning borrows, so a write would race. | `cannot reassign or mutate captured variable 'total' inside a parallel.* closure (captures are shared, non-owning borrows); return a value and combine it on the calling thread instead` | Return a value from the closure and combine the results on the calling thread. |

## Reserved codes

These codes are defined in the header and reserved inside their stages' ranges. Nothing reports them yet; they are listed here so the numbering is complete and nothing is grabbed for a different purpose later.

| Code | Name | Stage | Reserved for |
| ---- | ---- | ----- | ------------ |
| CP3001 | `kInternalError` | internal | Unexpected compiler-internal states; the last-resort code. |
| CP3203 | `kUnexpectedEof` | syntax | A dedicated "unexpected end of input" message; today truncated input surfaces as CP3201/CP3202 at the last token. |
| CP3205 | `kInvalidStatement` | syntax | A statement in a position where no statement can start. |
| CP3405 | `kNotIndexable` | types | Indexing a value whose type provides no index operation. |
| CP3412 | `kConstraintNotSatisfied` | types | A generic argument that does not satisfy its constraint (generics are still landing). |
| CP3413 | `kCannotInstantiateAbstract` | types | `new` on an abstract class. |
| CP3505 | `kUnreachableCode` | statements | Statements after `return`, `throw`, `break` or `continue`. |
| CP3602 | `kPackageNotFound` | modules | An import that should resolve to an installed package but does not. |
| CP3603 | `kNativeImportUnsupported` | modules | Native package imports in builds where native binding is unavailable. |
| CP3701 | `kMissingInterfaceMember` | classes/interfaces | A class that fails to implement an interface member. |
| CP3702 | `kPrivateFieldInLiteral` | classes/interfaces | A `#private` field name used in an object literal. |
| CP3703 | `kMultipleInheritance` | classes/interfaces | More than one base class; today the parser reports CP3201 instead. |
| CP3704 | `kInvalidOverride` | classes/interfaces | An `override` that does not match a base member. |
| CP3705 | `kDuplicateMember` | classes/interfaces | The same member declared twice in a class or interface. |
| CP3706 | `kMixedEnumBacking` | classes/interfaces | An enum mixing string and number values. |
| CP3802 | `kUndeclaredProperty` | banned JS patterns | Assigning to a field the class never declared (still a banned shape). Member misses on known receivers report CP3305; CP3802 stays reserved for its banned-pattern role. |
| CP3807 | `kGeneratorsUnsupported` | banned JS patterns | `function*` and `yield`; today the parser reports CP3201 with `generators are not supported`. |
| CP3808 | `kDecoratorsUnsupported` | banned JS patterns | `@decorator` syntax; today the parser reports CP3201 with `decorators are not supported`. |
| CP3903 | `kUnknownWorkerFile` | concurrency | `parallel.worker("FILE")` naming a file that cannot be found. |
