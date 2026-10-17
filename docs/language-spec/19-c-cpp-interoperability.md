# 19. C/C++ interoperability

C+++ is meant to plug into the existing native ecosystem rather than reinvent it:

```cp3
import native "opencv" as cv;
```

The compiler generates the necessary `#include`s and linker configuration. **This shows import syntax, not a claim that all of OpenCV works today** - OpenCV's real API leans heavily on operator overloading and templated types (`cv::Mat`'s `operator()`, `Mat_<T>`), and beta one's native-interop scope explicitly excludes both (below). A binding limited to OpenCV's plain free functions with simple parameter types is realistic for beta one; the full class-based API is post-beta-one bindgen work ([CLI-README.md §6](../cli-tooling/06-open-tooling-questions.md)).

## Larger libraries (Qt, Unreal Engine, etc.)

These aren't rewritten - they're wrapped:

```
C+++ bindings
   ↓
C++ API
   ↓
Unreal Engine / Qt / etc.
```

```cp3
import "ue5" as UE;
let actor = new UE.Actor();
```

The C+++ developer only ever sees C+++.

## Native pointer safety rule

`import native` cannot make every C++ pointer automatically safe just by crossing the boundary - a binding layer has to classify *what kind* of pointer it's actually dealing with, and wrap each kind differently. Five shapes cover the real cases:

| Native C++ shape | cp3-facing wrapper |
|---|---|
| **Owned pointer** you must free (`Thing* create(); void destroy(Thing*);`) | An ordinary object whose destructor calls the matching free function automatically - the user never sees `Thing*`. |
| **Borrowed pointer + known length** (`const uint8_t* getData(size_t* len);`) | A scoped view handed to a callback (`data.withBytes((bytes) => {...})`) - the compiler rejects any attempt to store, return, or capture `bytes` beyond the callback - a narrow restriction on this one scoped-view type (the same borrow-only idea `parallel.*` captures rely on, [§13](13-concurrency-parallel-and-workers.md)), not a general lifetime-analysis pass. |
| **Borrowed pointer for one call only** (`void process(const uint8_t*, size_t);`) | The same scoped-view pattern, with the binding author responsible for guaranteeing the native function doesn't retain the pointer past the call - a binding that lies about this breaks safety, same as any FFI boundary in any language. |
| **Unknown/untyped `void*`** | Cannot be made safe without the binding author supplying real metadata (what it is, how it's freed, its size). Without that metadata, it's `unsafe`-only ([§12](12-pointers-references.md)). |
| **Device/non-CPU pointers** (e.g. CUDA device memory) | Wrapped as an opaque buffer object (`cuda.allocBuffer(...)`, `.copyFrom(...)`, `.copyTo(...)`) - the actual device pointer never appears as a cp3 value, since it isn't even a valid address in this process's own memory space to begin with. |

This gives three concrete levels, not just an assertion that "pointers are hidden":

1. **Safe normal language** - `let`/`const`/arrays/objects, no addresses, no pointers, ever (this is what essentially all cp3 code is).
2. **Safe advanced memory** - native-interop's scoped views (the table above) - a real interop tool, still fully memory-safe because the reference can't outlive its owner.
3. **`unsafe` escape hatch** - raw addresses and untyped pointers, explicitly gated ([§12](12-pointers-references.md)), for the cases that genuinely can't be wrapped any other way.

The underlying rule, stated plainly: **native pointers are allowed inside generated C++; they are never allowed as a value a normal cp3 program can hold.** If a native API can't be classified into one of the safe shapes above, the binding must require an explicit `unsafe` block to touch it - that's a deliberate, visible cost, not a gap in the design.

