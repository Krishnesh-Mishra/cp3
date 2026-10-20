# 13. Concurrency: `parallel.*` and workers

Two distinct concurrency models, chosen based on the shape of the task - never one mechanism trying to cover both:

- **`parallel.*`** - shared-memory, structured data parallelism, for splitting one computation across CPU cores
- **`worker`** - isolated, message-passing tasks, for independent background jobs

## `parallel.*` - shared-memory data parallelism

Backed internally by TBB (Threading Building Blocks), wrapped behind three narrow, structured operations - never raw thread creation, and never exposing locks, mutexes, or atomics to the user:

```cp3
// Apply the same operation across a range - tbb::parallel_for underneath
parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;
});

// Split a computation across a range and safely combine results - tbb::parallel_reduce underneath
let total = parallel.reduce(1..100000, 0, (sum, i) => sum + i);

// Run several independent functions concurrently - tbb::parallel_invoke underneath
let results = parallel.run(
  () => sumRange(1, 100),
  () => factorial(30),
  () => alternatingSum(1, 100)
);
```

`parallel.reduce` gives each thread its own local accumulator and combines them safely at the end - the developer never manually synchronizes anything; the compiler generates the thread-local-accumulate-then-merge pattern automatically.

**Capture safety, precisely:**

**Rule 1 - closures passed to `parallel.*` always take a raw, non-owning borrow of anything captured, never a refcounted handle.** Atomic refcounts ([§10](10-memory-management.md)) would make retaining a captured object from several threads *correct*, but it would also be slow in exactly the place speed matters: every core hitting the same refcount cache line on every iteration serializes the very parallelism `parallel.*` exists to provide. And it is unnecessary - every `parallel.*` call is **synchronous**; the calling thread blocks until every closure finishes, so any value already alive in the calling scope is *already* guaranteed to outlive the whole operation (the same reasoning the native pointer safety rule, [§19](19-c-cpp-interoperability.md), relies on for scoped views). Reading a captured array/object from many closures at once therefore costs nothing and touches no refcount.

**Rule 2 - the one named safe-write exception.** Writing `numbers[i] = numbers[i] * 2` inside `parallel.for(0..numbers.length, (i) => {...})` **is** writing to a shared captured array from multiple closures, which is otherwise banned, because the borrow in Rule 1 protects the *lifetime* of the object but not unsynchronized mutation of its *contents* (atomic refcounts don't help there). The specific case that is provably safe and explicitly allowed: **writing through `parallel.for`'s own provided loop index** - every iteration gets a distinct `i`, so no two closures can ever touch the same element; no aliasing is possible, by construction of the loop itself. **Anything else** - writing through a separately computed/stored index, calling a mutating method on a captured shared object (`sharedList.push(...)`), or writing to a field rather than an indexed element - remains a compile error, since the compiler has no general way to prove those writes are disjoint:

```cp3
let numbers = [1, 2, 3, 4, 5];

parallel.for(0..numbers.length, (i) => {
  numbers[i] = numbers[i] * 2;   // ✓ allowed - writes through the loop's own index, provably disjoint
});

let sharedList = [1, 2, 3];

parallel.run(
  () => { sharedList.push(10); },  // ✗ compile error - not an index write, can't be proven disjoint
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

**Reassigning a captured `let`/`var` inside a `parallel.*` closure is a compile error too** - the most common version of the same mistake, spelled out because it is the one a JS developer will reach for first:

```cp3
let total = 0;
parallel.for(0..1000, (i) => {
  total = total + 1;   // ✗ compile error - shared mutable capture; use parallel.reduce
});

let total2 = parallel.reduce(0..1000, 0, (sum, i) => sum + 1);   // ✓ each thread accumulates privately, merged at the end
```

Reading a captured `let`/`var`/`const` from many closures at once is fine, since nothing writes to it.

**`let`, `var`, and `const` are never atomic - no binding keyword changes how a variable is accessed across threads.** Making `let`/`var` atomic so parallel closures could update them directly was considered and rejected, for three reasons:

- **Everyone would pay for it.** Almost all code never touches a second thread, yet every `let` in every program would carry the atomic cost - against the "no runtime tax if you don't use it" promise ([§1](01-philosophy.md)).
- **It would not make code correct.** Atomic reads and writes do not make a *sequence* safe: `if (balance > 50) { balance = balance - 50; }` run from two threads can still overdraw, because the check and the update are separate steps. Fixing that takes a lock, and cp3 deliberately exposes no locks ([§13](13-concurrency-parallel-and-workers.md) intro). Atomic variables would only give a false sense of safety.
- **It cannot be applied uniformly.** `std::atomic` only works on small trivially-copyable types, so atomic `let` would work for `number` but not for `string`, arrays, objects, or class instances - an inconsistent rule to teach. (And ten cores updating one atomic variable still queue on the same cache line, so it would often be slower than single-threaded code.)

The supported tools are the ones above: read-only sharing, `parallel.reduce` for combining, and the disjoint loop-index write for filling arrays.

## `worker` - isolated threads with message events (decided - D80)

Rather than sharing memory between threads (which would leave two threads free to mutate the same object with no safe way to coordinate, since cp3 exposes no locks or atomics to users), workers avoid shared memory entirely. Each worker runs on its own real OS thread with its own independent event loop ([§14](14-async.md)) - not a separate OS process. A thread is far cheaper to spawn than a process, and the safety a worker provides comes from the compiler's discipline around what's reachable inside it, not from OS-level process isolation. The tradeoff this accepts: a hard crash inside a worker thread can take the whole program down with it, the same as any other unhandled crash - full process-level fault isolation isn't part of what workers provide in beta one. A thrown handler error is not a crash: it is delivered to the other side as a catchable `"error"` event (below).

Workers are explicit handles, the JS Web Worker model: `new Worker(path)` starts the thread and returns a `Worker<In, Out>` handle on the main side; messages go both ways with `postMessage` and arrive as events. The worker unit's default export is the worker body - it runs once at thread start and receives the worker-side handle - and its one annotation, `thread: Thread<In, Out>` (`In` is main-to-worker, `Out` is worker-to-main), declares the typed channel both sides speak.

```cp3
// sum.cp3 - the worker unit; the entry annotates the worker-side handle
export default (thread: Thread<{ n: number }, { total: number }>) => {
  thread.on("message", (msg) => {
    thread.postMessage({ total: msg.n * 2 });
  });
};
```

```cp3
// main.cp3 - Worker<{ n: number }, { total: number }> is inferred; no annotations here
const w = new Worker("./sum.cp3");
w.on("message", (msg) => {
  console.log(`${msg.total}`);          // prints 42
});
w.on("error", (err) => {
  console.log("worker error", err);
});
w.postMessage({ n: 21 });
// the main event loop keeps running between messages
w.terminate();
```

- `new Worker(path)` takes a **static string literal** naming a `.cp3` file, resolved at compile time like an import - a computed path is a compile error. Starting a worker is explicit: `new Worker` is the only way, and an ordinary default import of a `.cp3` file stays an ordinary cross-module call. The earlier call shape (`import worker from "./x.cp3"; await worker(v)`) and its binding-name inference are **removed**.
- `thread: Thread<In, Out>` is the **single source of truth** for the channel: the main side infers `Worker<In, Out>` from the resolved worker unit, so main-side code needs no annotations of its own, and a missing annotation on the worker entry (`export default` or inline arrow) is a compile error.
- **`void` in one direction makes that direction's send surface a compile error**: a `Thread<In, void>` worker cannot `thread.postMessage`, and `w.postMessage` / `w.request` on a `Worker<void, Out>` cannot send. (`Thread<unknown, unknown>` waits for the runtime unknown carrier, opt-in, later.)
- **Payloads are ordinary copyable cp3 value types** - numbers, strings, booleans, arrays, records, enums, unions of those - and **copy on send exactly like an assignment copy**. Functions, closures, promises, live handles and unresolved (`unknown`) payload types are compile errors.
- `w.postMessage(v)` / `thread.postMessage(v)` sends a structured-clone **deep copy**; messages queue until the other side attaches a listener and arrive **FIFO per sender**. Nothing blocks the main loop: it keeps turning between messages.
- `w.on("message", listener)` / `w.on("error", listener)` exist on both sides (`w` on the main thread, `thread` inside the worker); multiple listeners may be attached. A thrown error inside a message handler arrives on the other side as a catchable `"error"` event, never as a silent death; the throwing side's loop keeps serving.
- `await w.request(v)` is the optional **one-shot ask/answer sugar** over the same channel (correlation-tagged); it resolves with the next `Out` message, so code that just wants one answer does not manage events by hand. On the worker side the request arrives as an ordinary `"message"` event.
- `w.terminate()` stops the worker's loop and frees its thread; pending messages are dropped; it is idempotent. Termination is the main side's: `thread` has only `on` and `postMessage`. A live worker keeps the main loop alive until `w.terminate()` (or the worker unit ending), and the process exits when the loop is idle and no handles remain.
- **The inline arrow compiles through the same worker-unit pipeline as a file** - a synthesized unit with its own module state - and has **no access to enclosing locals**: pass data with messages; a reference to an enclosing binding is a compile error. The file form stays the recommendation for anything with structure; moving from inline to file is moving the body and swapping the arrow for a path.

```cp3
// inline form - same worker-unit pipeline; the main side still infers Worker<number, number>
const w = new Worker((thread: Thread<number, number>) => {
  thread.on("message", (n) => {
    thread.postMessage(n * 2);
  });
});
```

Whatever crosses a worker boundary is a **genuine deep copy** in both directions, not a shared reference - nothing else is implicitly reachable from inside the worker. This has to be a real copy rather than a shared handle across the thread boundary: atomic refcounts ([§10](10-memory-management.md)) keep the *counter* safe, but two threads mutating the same object's fields would still be a data race, and cp3 deliberately gives users no locks to prevent one. This mirrors how JS Web Workers already behave (message-passing, not shared memory), so it matches the existing JS mental model. Module state follows the same isolation: every worker holds its own copy of its unit's module-level bindings.

Status: the handle wave (M2-PLAN) implements this surface (**D80** and its typed-channel addendum); the lead flips this paragraph to landed at landing. The earlier call-shaped form (`import worker from "./x.cp3"; await worker(v)`) is removed, not deprecated.

## When to use which

| Need                                                                         | Use                                      |
| ---------------------------------------------------------------------------- | ---------------------------------------- |
| Same operation across a big array/range                                      | `parallel.for`                           |
| Same computation, combine results (sum, max, etc.)                           | `parallel.reduce`                        |
| A few different, independent functions at once                               | `parallel.run`                           |
| One long-running, independent thread you talk to over time                   | `new Worker(...)` + `on`/`postMessage`   |
| One isolated task, one answer back                                           | `new Worker(...)` + `await w.request(v)` |

## Servers across threads: `createServer({ threads })` (decided, queued - D79)

The `std/network` server is single-loop by default, and that stays the exact
default behavior. A server can opt into one event loop per OS thread:

```cp3
const app = createServer({ threads: os.cpus() });
```

`threads` must be an integer >= 1; `1` and the absent option mean today's
single-loop server. With `n` threads, each loop runs its own server instance
with its own module-level state - the same isolation `worker` already has, so
`n` threads means `n` copies of every module-level binding and cross-loop state
goes through `worker` message passing, never shared memory. A connection is
pinned for its whole lifetime to the loop that accepted it (WebSocket upgrades
included), which keeps handlers and per-connection state single-threaded. No
locks, atomics, or shared closures are exposed, exactly as everywhere else in
this section; `parallel.*` remains the CPU data-parallelism mechanism, and
`threads` exists only to scale I/O across cores. Platform binding: POSIX uses
`SO_REUSEPORT` per loop, Windows uses one shared listening socket with accept
handoff. Full decision record: D79 in `docs/rough/DECISIONS.md`.

