---
title: Errors and exceptions
chapter: Chapter 1 · The language
---
# Errors and exceptions

CP3 reports source and type errors during compilation, and its runtime can produce catchable failures for supported operations. A compiler diagnostic and a runtime exception are different events and are handled by different parts of the program.

## Compiler diagnostics

The compiler prints a stable line containing the source path, line, column, severity, diagnostic code, and message. In an interactive terminal it may also render a source excerpt and caret. The machine-readable diagnostic line is preserved for tools and tests.

When an error is reported, the CLI writes a standalone HTML page in the operating system’s temporary directory. It includes the exact diagnostic and ranks three documentation sections by TF-IDF cosine similarity against the diagnostic text. The scores indicate text overlap; they are not confidence probabilities. The page content is useful even when the local docs server is not running.

Run `cp3 docs` to browse the bundled language guide. For a type or syntax issue, fix the earliest relevant message first because later diagnostics may be follow-on errors.

## Runtime failures

Runtime failures are used for operations whose error depends on a value at execution time, such as an invalid file operation, an unsupported dynamic buffer encoding, or an out-of-range typed-array view construction. The exact failure mechanism is defined by that API family.

## `try` and `catch`

CP3 has supported exception-handling syntax, but the thrown value forms and catch behavior are constrained by the language’s error model. Use typed boundaries and test both success and failure paths. Do not assume any JavaScript value can be thrown and recovered in exactly the same way as in Node.

## Error values from standard-library calls

Some calls return a promise that rejects on failure; others resolve with a result object that contains an exit code. For example, `process.exec` rejects when a process cannot be launched, but a child that exits non-zero resolves with its `exitCode`. Read each API page to distinguish these cases.

## Writing reliable error paths

1. Decide whether the caller should handle a result value or a thrown/rejected failure.
2. Preserve the original error context when adding a message.
3. Do not catch and discard failures unless the program has a recovery action.
4. Add tests for the exact failure condition and output contract.

## Throwing and catching

A `throw` transfers control to the nearest matching `catch`. The caught binding is scoped to that catch body. The conformance suite includes strings and numbers as thrown values, so do not assume the only catchable object is a JavaScript `Error` instance:

```ts
try {
  throw "unavailable";
} catch (reason) {
  console.log(reason);
}
```

A throw from a called function propagates to an enclosing catch unless an inner handler catches it. A throw raised inside a catch can be handled by an outer catch. These cases are exercised in `tests/cases/errors`.

## `finally`

A `finally` block runs when control leaves its associated try/catch through normal completion, a caught throw, or a return, according to the tested CP3 behavior:

```ts
function loadValue(): number {
  try {
    return 7;
  } finally {
    console.log("cleanup");
  }
}
```

Use `finally` for cleanup that must happen across both success and failure paths. Do not return a different value from cleanup unless the language contract and test make that behavior intentional.

## Runtime errors and catchability

CP3 runtime helpers can throw boxed CP3 values for operations with runtime-dependent failure. Examples include filesystem failure, malformed dynamic encoding, invalid crypto envelopes, and typed-buffer access failures. The API page determines whether a failure throws synchronously, rejects a promise, or is represented in a result object.

A compiler diagnostic cannot be caught by source code because it prevents the program from being built. Fix parsing and type errors before debugging a `try`/`catch` path.

## Promise rejection is a separate path

An asynchronous function may reject rather than throw synchronously at the call site. `await` observes that rejection in the async control flow; use the supported `try`/`catch` around the awaited operation when local recovery is intended. Some APIs resolve to a result containing a nonzero exit status instead of rejecting; `process.exec` is one such example for a child that starts and exits unsuccessfully.

Do not write one generic catch policy for all APIs. Read whether the specific operation throws, rejects, or returns a status, and test each documented failure branch.

## Error handling checklist

- Keep the original diagnostic or error value when adding context.
- Catch only where there is a recovery, translation, or cleanup action.
- Do not silently convert malformed input to a valid-looking default.
- Preserve resource cleanup through `finally` when it must run on returns or throws.
- Add tests for the nearest catch, propagation, rethrow, and cleanup paths affected by a change.

The language fixtures under `tests/cases/errors` cover thrown primitives, catch scope, nested handlers, propagation, finally on return, and runtime failures such as out-of-bounds operations. Use them as the contract before changing the lowering.
