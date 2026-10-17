# Why C+++?

##### **JS/TS has the largest developer base and the gentlest learning curve of any mainstream language.** Most other languages - C++, Rust, Go, Java - force that developer to relearn how to declare a variable, write a loop, define a class, work with arrays. The concepts are the same everywhere; the syntax isn't. That relearning cost is the real barrier, not the underlying concepts.

**But JS/TS pays a steep runtime tax.** A plain Express server doing trivial request handling can sit at hundreds of MB of memory - not because the task is hard, but because V8 is a full JIT runtime: garbage collector, dynamic typing machinery, JIT warmup, object shapes, etc. all running underneath even the simplest code.

**Go tried to fix some of this and still ships a garbage collector.** GC pauses and GC memory overhead are exactly the category of cost C+++ is trying to avoid, not just relocate.

**Rust and C++ get you real performance with no GC - but at a steep syntax/learning cost.** Ownership/borrow-checking syntax (Rust) and manual memory/pointer syntax (C++) are precisely the "relearn everything" tax that keeps most JS/TS developers away, even though the underlying tasks (an if-statement, a for-loop, pushing to an array) aren't conceptually hard at all.

**The gap C+++ targets:** keep the syntax ~99.9% identical to JS/TS - `let`, `const`, `for`, `if`, `array.push(...)`, classes - so a JS/TS developer's existing muscle memory transfers directly, while compiling to real native code with no garbage collector . The things that are genuinely hard - the internals of Unreal Engine, Qt, OpenCV, or an ML runtime - stay hard and stay in C++. What C+++ targets is everything *around* that: the everyday application/server code that's needlessly expensive today (like the Express example above) purely because it's running on a dynamic runtime rather than being compiled.

---

