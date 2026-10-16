# 7. Error handling

Same model as JavaScript — `try` / `catch` / `finally`:

```cp3
try {
  riskyOperation();
} catch (e) {
  console.log("Failed:", e);
} finally {
  cleanup();   // runs whether the try succeeded or threw — always
}
```

No separate exception-type hierarchy beyond what this already gives — `throw` raises, the nearest enclosing `catch` handles it, same as JS.

**`finally` compiles via a generated C++ scope guard, not a native language feature** — C++ has no `finally` keyword, but its destructors already run unconditionally on scope exit (normal return or exception unwinding alike). The compiler generates a small internal struct whose destructor runs the `finally` block's code, and constructs one instance of it right where the `finally` block sits — the same RAII mechanism [§10](10-memory-management.md) already relies on for memory, applied here for arbitrary cleanup code instead of just destruction. This is why `finally` reliably runs even when the `try` block returns early or throws past the `catch`.

