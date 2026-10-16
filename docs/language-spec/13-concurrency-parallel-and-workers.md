# 13. Concurrency: `parallel.*` and workers

Two distinct concurrency models, chosen based on the shape of the task — never one mechanism trying to cover both:

- **`parallel.*`** — shared-memory, structured data parallelism, for splitting one computation across CPU cores
- **`worker`** — isolated, message-passing tasks, for independent background jobs

## `parallel.*` — shared-memory data parallelism

Backed internally by TBB (Threading Building Blocks), wrapped behind three narrow, structured operations — never raw thread creation, and never exposing locks, mutexes, or atomics to the user:

```cp3
// Apply the same operation across a range — tbb::parallel_for underneath
parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;
});

// Split a computation across a range and safely combine results — tbb::parallel_reduce underneath
let total = parallel.reduce(1..100000, 0, (sum, i) => sum + i);

// Run several independent functions concurrently — tbb::parallel_invoke underneath
let results = parallel.run(
  () => sumRange(1, 100),
  () => factorial(30),
  () => alternatingSum(1, 100)
);
```

`parallel.reduce` gives each thread its own local accumulator and combines them safely at the end — the developer never manually synchronizes anything; the compiler generates the thread-local-accumulate-then-merge pattern automatically.

**Capture safety, precisely:**

**Rule 1 — closures passed to `parallel.*` always take a raw, non-owning borrow of anything captured, never a refcounted handle.** Atomic refcounts ([§10](10-memory-management.md)) would make retaining a captured object from several threads *correct*, but it would also be slow in exactly the place speed matters: every core hitting the same refcount cache line on every iteration serializes the very parallelism `parallel.*` exists to provide. And it is unnecessary — every `parallel.*` call is **synchronous**; the calling thread blocks until every closure finishes, so any value already alive in the calling scope is *already* guaranteed to outlive the whole operation (the same reasoning the native pointer safety rule, [§19](19-c-cpp-interoperability.md), relies on for scoped views). Reading a captured array/object from many closures at once therefore costs nothing and touches no refcount.

**Rule 2 — the one named safe-write exception.** Writing `numbers[i] = numbers[i] * 2` inside `parallel.for(0..numbers.length, (i) => {...})` **is** writing to a shared captured array from multiple closures, which is otherwise banned, because the borrow in Rule 1 protects the *lifetime* of the object but not unsynchronized mutation of its *contents* (atomic refcounts don't help there). The specific case that is provably safe and explicitly allowed: **writing through `parallel.for`'s own provided loop index** — every iteration gets a distinct `i`, so no two closures can ever touch the same element; no aliasing is possible, by construction of the loop itself. **Anything else** — writing through a separately computed/stored index, calling a mutating method on a captured shared object (`sharedList.push(...)`), or writing to a field rather than an indexed element — remains a compile error, since the compiler has no general way to prove those writes are disjoint:

```cp3
let numbers = [1, 2, 3, 4, 5];

parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;   // ✓ allowed — writes through the loop's own index, provably disjoint
});

let sharedList = [1, 2, 3];

parallel.run(
  () => { sharedList.push(10); },  // ✗ compile error — not an index write, can't be proven disjoint
  () => { sharedList.push(20); }
);
```

The correct pattern is always to return data and let the calling thread combine it, never mutate a captured outer variable:

```cp3
let results = parallel.run(
  () => computePartA(),
  () => computePartB()
);
let combined = results[0] + results[1];
```

**Reassigning a captured `let`/`var` inside a `parallel.*` closure is a compile error too** — the most common version of the same mistake, spelled out because it is the one a JS developer will reach for first:

```cp3
let total = 0;
parallel.for(0..1000, (i) => {
  total = total + 1;   // ✗ compile error — shared mutable capture; use parallel.reduce
});

let total2 = parallel.reduce(0..1000, 0, (sum, i) => sum + 1);   // ✓ each thread accumulates privately, merged at the end
```

Reading a captured `let`/`var`/`const` from many closures at once is fine, since nothing writes to it.

**`let`, `var`, and `const` are never atomic — no binding keyword changes how a variable is accessed across threads.** Making `let`/`var` atomic so parallel closures could update them directly was considered and rejected, for three reasons:

- **Everyone would pay for it.** Almost all code never touches a second thread, yet every `let` in every program would carry the atomic cost — against the "no runtime tax if you don't use it" promise ([§1](01-philosophy.md)).
- **It would not make code correct.** Atomic reads and writes do not make a *sequence* safe: `if (balance > 50) { balance = balance - 50; }` run from two threads can still overdraw, because the check and the update are separate steps. Fixing that takes a lock, and cp3 deliberately exposes no locks ([§13](13-concurrency-parallel-and-workers.md) intro). Atomic variables would only give a false sense of safety.
- **It cannot be applied uniformly.** `std::atomic` only works on small trivially-copyable types, so atomic `let` would work for `number` but not for `string`, arrays, objects, or class instances — an inconsistent rule to teach. (And ten cores updating one atomic variable still queue on the same cache line, so it would often be slower than single-threaded code.)

The supported tools are the ones above: read-only sharing, `parallel.reduce` for combining, and the disjoint loop-index write for filling arrays.

## `worker` — isolated, message-passing tasks, one real OS thread each

Rather than sharing memory between threads (which would leave two threads free to mutate the same object with no safe way to coordinate, since cp3 exposes no locks or atomics to users), `worker` avoids shared memory entirely. Each `worker` call spins up its own real OS thread, running its own independent event loop ([§14](14-async.md)) — not a separate OS process. A thread is far cheaper to spawn than a process, and the safety `worker` provides comes from the compiler's discipline around what's reachable inside it, not from OS-level process isolation. The tradeoff this accepts: a crash inside a worker thread can take the whole program down with it, the same as any other unhandled crash — full process-level fault isolation isn't part of what `worker` provides in beta one.

```cp3
// worker.cp3
export default (data) => {
  // process data, return a result
  return processedResult;
};
```

```cp3
// main.cp3
import worker from "worker.cp3";

const output = await worker(dataNeededForProcessing);
```

`worker(...)` returns a `Promise` ([§14](14-async.md)), resolving with whatever the worker file's default export returns, or rejecting if the worker file throws — a plain `try`/`catch` around the `await` catches it, exactly like any other async call (`fs.readFile`, `process.exec`). No separate event/handle mechanism — it's the same Promise shape as everything else in std.

Whatever crosses into a worker file is **copied in as a genuine deep copy**, not a shared reference — nothing else is implicitly reachable from inside it. This has to be a real copy rather than a shared handle across the thread boundary: atomic refcounts ([§10](10-memory-management.md)) keep the *counter* safe, but two threads mutating the same object's fields would still be a data race, and cp3 deliberately gives users no locks to prevent one. This mirrors how JS Web Workers already behave (message-passing, not shared memory), so it matches the existing JS mental model.

## When to use which

| Need                                                                         | Use               |
| ---------------------------------------------------------------------------- | ----------------- |
| Same operation across a big array/range                                      | `parallel.for`    |
| Same computation, combine results (sum, max, etc.)                           | `parallel.reduce` |
| A few different, independent functions at once                               | `parallel.run`    |
| One long-running, independent background task, isolated from everything else | `worker`          |

