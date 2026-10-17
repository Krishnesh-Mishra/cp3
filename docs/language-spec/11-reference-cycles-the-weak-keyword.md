# 11. Reference cycles & the `weak` keyword

Reference counting frees an object when nothing strong points at it - which means two objects pointing *at each other* keep each other alive forever, even after every outside variable is gone:

```cp3
let a = {};
let b = { friend: a };
a.friend = b;      // a ↔ b: each holds the other
b = null;
a = null;          // both outside variables are gone, but a.friend still keeps b alive
                   // and b.friend still keeps a alive - neither count reaches 0: a leak
```

**Every cycle must be broken explicitly, by the programmer, with `weak`.** The compiler never infers, guesses, or auto-assigns it - in particular it does *not* look at class declaration order or at the type graph. An earlier draft auto-marked the "second" class of a parent/child cycle as weak; that was dropped because the strong/weak assignment silently depended on which class happened to be declared first, so reordering code could turn a working program into a leak ([§23](23-open-design-questions.md)).

**Declared fields - a `weak` modifier on the field:**

```cp3
class Parent {
  child: Child;          // strong (the default) - Parent keeps Child alive
}
class Child {
  weak parent: Parent;   // weak - Child does NOT keep Parent alive
}
```

**Object literals / untyped links - the `{ weak: value }` wrapper:**

```cp3
let song1 = { next: null, prev: null };
let song2 = { next: null, prev: null };
let song3 = { next: null, prev: null };

song1.next = song2;
song2.next = song3;
song3.next = song1;   // loop, but 'next' keeps the list alive/ordered - leave strong

song2.prev = song1;
song3.prev = song2;
song1.prev = { weak: song3 };   // 'weak' link - doesn't keep song3 alive on its own
```

**What `weak` actually means, mechanically:** every managed value has a hidden counter - "how many strong links currently point at me." A normal link adds 1 to that counter; a `weak` link points to the value too, but does not add to the counter. When the counter reaches 0, the value is destroyed - even if some `weak` links to it still exist (they simply become unusable once that happens). Marking one side of a loop as `weak` is what lets the counter actually reach 0 once nothing outside the loop is using it, instead of the two (or more) objects propping each other up forever.

**Reading a `weak` field after its target is gone:** the read yields `T | undefined` - the ordinary optional type ([§3](03-type-system.md)), not a special "dangling" value. While the target is still alive the field behaves like a normal reference; once every strong link is dropped and the target is destroyed, the `weak` field reads as `undefined`, and the caller narrows before use exactly as with any other `T | undefined`. This is the decided beta-one semantics.

**Cycle diagnostics:** the compiler emits a **non-fatal warning** when it can prove a reference-type cycle has no `weak` edge anywhere in it - a "this cycle may leak; mark one side `weak`" hint. This is a diagnostic only: it never fails a build, and it is **not** inference - the compiler still never auto-assigns `weak` or flips strong/weak based on declaration order (see above, and [§23](23-open-design-questions.md)). A cycle the compiler cannot prove is left alone silently.

This is not a design flaw - every language that avoids a full garbage collector hits this same wall and takes the same approach: Rust requires `Weak<T>` explicitly to break cycles; Swift's ARC requires explicit `weak`/`unowned` markers. Full GC languages (JS, Java) avoid this only by paying for a runtime reachability scan instead - the exact cost cp3 is built to avoid. `weak` compiles down to `std::weak_ptr` under the hood, reusing C++'s existing, battle-tested mechanism rather than inventing a new one.


