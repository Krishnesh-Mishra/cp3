# 18. Modules & imports

Instead of dumping everything into a global `std::` namespace, imports resolve to namespaced module objects:

```cp3
import console from "std/console";
console.log("Hello");

import Math from "std/math";
let x = Math.sqrt(25);
```

```cp3
import "cpp/iostream" as io;
import "cpp/filesystem" as fs;

io.println("Hello");
fs.createDirectory("test");
```

These module objects don't necessarily need to exist at runtime - resolution can happen entirely at compile time.

## Prelude files: `_prelude.cp3`, `_prelude.same.cp3`, `_prelude.weak.cp3`

A folder can contain a special prelude file whose contents (typically `import` statements, but not limited to that) are automatically injected at the top of every `.cp3` file in scope - removing the need to repeat the same imports in every file of a folder.

Three variants, differing only in how far the injection reaches:

**`_prelude.cp3` - strong.** Injects into every `.cp3` file in the same folder **and every subfolder, recursively, no matter how deep**. Nothing can stop it from reaching further down the tree.

```
src/
  _prelude.cp3        ← reaches utils.cp3, helpers/math.cp3, helpers/deep/calc.cp3 - everything under src/
  utils.cp3
  helpers/
    math.cp3
    deep/
      calc.cp3
```

**`_prelude.same.cp3` - folder-only.** Injects only into `.cp3` files directly in the same folder - does **not** cascade into subfolders at all.

```
src/
  _prelude.same.cp3   ← reaches utils.cp3 only
  utils.cp3
  helpers/
    math.cp3           ← does NOT get this injection
```

**`_prelude.weak.cp3` - recursive, but stoppable.** Like the strong version, it cascades into subfolders - but if a subfolder contains **any** prelude file of its own (even a completely empty one), that acts as a wall: the weak injection stops there and does not reach any file at or beyond that subfolder.

```
src/
  _prelude.weak.cp3   ← reaches utils.cp3 and helpers/math.cp3
  utils.cp3
  helpers/
    math.cp3            ← gets src/'s weak prelude (no wall here)
    walled/
      _prelude.cp3         ← empty or not, its mere presence walls off this subtree
      thing.cp3              ← does NOT get src/'s weak prelude
      deeper/
        x.cp3                  ← also walled off - the block applies to the whole subtree past the wall
```

**Multiple prelude files in one folder are allowed and stack together, including strong + weak at once** - confirmed not a conflict. Injection order: **`_prelude.same.cp3` first, then `_prelude.cp3` (strong), then `_prelude.weak.cp3`** - folder-local content first, then the two recursive variants. When strong and weak coexist, each simply follows its own already-defined rule independently: strong's content keeps recursing unconditionally no matter what's below it; weak's content also recurses, but still stops wherever a deeper folder plants its own prelude file - the two don't interact or override each other, they just both apply, strong's injection landing before weak's.

