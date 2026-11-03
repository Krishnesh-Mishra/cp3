---
title: Garbage collection: what CP3 does not provide
chapter: Chapter 2 · Runtime and memory
---
# Garbage collection: what CP3 does not provide

CP3 does not have a tracing garbage collector. Reference counts release acyclic shared values when the last strong handle is destroyed. A strong cycle can keep objects alive because each member continues to hold another member.

## Reference counting is deterministic but not cycle collecting

With reference counting, an object’s strong count reaches zero when no strong handle remains. Its destructor then runs. This gives prompt release for ordinary acyclic ownership graphs, but every copy and drop has a cost and cycles need explicit design.

## Break cycles deliberately

Use a weak back-reference when the relationship is non-owning. For example, a parent may strongly own children while a child keeps only a weak link to the parent. That way the child does not keep the whole graph alive after the owner releases it.

## Escape analysis is an optimization, not a collector

Escape analysis can remove selected local allocations by proving that an object need not outlive its stack frame. It does not periodically scan or reclaim unreachable graphs. An optimization’s failure to trigger does not change the language lifetime semantics.

## Practical review checklist

- Identify which reference owns each object.
- Look for strong back-links in parent/child or observer graphs.
- Use weak references for non-owning links where supported.
- Keep async tasks and returned closures in mind when reasoning about lifetime.
- Profile allocation-heavy paths; do not infer that “no GC” means “no allocation cost.”

## Reachability and ownership graph

A reference count answers how many strong handles point to an object; it does not ask whether that object is reachable from a program root. If two objects point strongly to each other and all outside handles are dropped, each internal edge still contributes to the other's count. The graph remains allocated even though application code can no longer reach it.

Draw ownership edges when reviewing a long-lived object graph. An arrow marked strong means the field keeps its target alive. An arrow marked weak means it does not own the target and the read can be absent. This is especially useful for parent/child, observer/subject, and cache/back-reference designs.

## What gets released automatically

For an acyclic graph, dropping the last strong reference to an object reaches zero and destroys that object. Destroying its fields releases the strong references they own, which may cause their targets to reach zero in turn. This is ordinary reference-count destruction, not a background collection cycle.

Do not depend on one exact destruction ordering among unrelated objects unless the owning structure establishes it. If cleanup order matters, call an explicit cleanup method or use a scoped resource API whose contract provides that order.

## No hidden tracing pass

CP3 does not periodically scan stacks, globals, arrays, or closure environments for reachable objects. There is no tracing collector that discovers and reclaims an unreachable cycle later. “Not reachable by my source variable” is therefore not sufficient proof that a reference-counted cycle will be freed.

Escape analysis is a compile-time representation choice for narrowly proven local values. It neither runs at program shutdown nor collects heap graphs. A transformation that does not trigger leaves the ordinary reference-counting behavior intact.

## Practical memory review

- Identify strong owners and intended lifetime for each object.
- Use weak links only for relationships that may legitimately disappear.
- Break cycles by changing the ownership direction, not by periodically clearing unrelated global state.
- Avoid retaining large arrays or closures in caches after their use ends.
- Check whether an async continuation holds a closure and thereby extends captured values.
- Measure peak resident memory as well as execution time for churn workloads.

A low allocation count does not guarantee low memory use if a long-lived owner retains the objects. Conversely, promptly released acyclic objects can still have high allocation and reference-count costs.

## Related pages

- [Reference counts, cycles, and threads](03-reference-ownership.md)
- [Weak links and reference cycles](09-reference-cycles-and-weak-links.md)
- [Closure storage and lifetime](04-closures.md)
- [Inspecting runtime costs](11-inspecting-runtime-costs.md)
