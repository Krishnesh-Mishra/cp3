# binary-trees - recursive allocation and tree traversal

Allocation-heavy workload in the shape of the classic binary-trees benchmark.
Every language builds the same full binary trees from heap-allocated nodes,
checks them recursively, and prints one integer.

## Workload

Constants:

```text
MAX_DEPTH = 13
MIN_DEPTH = 4
```

A tree node is either a leaf or a node with exactly two children. Build a full
tree of depth `d` recursively:

```text
makeTree(0)     = leaf
makeTree(d)     = node(makeTree(d - 1), makeTree(d - 1))   for d > 0
```

Check a tree recursively:

```text
checkTree(leaf) = 1
checkTree(node) = 1 + checkTree(left) + checkTree(right)
```

The full computation:

```text
stretchDepth = MAX_DEPTH + 1
stretchCheck = checkTree(makeTree(stretchDepth))

longLived    = makeTree(MAX_DEPTH)
longLivedCheck = checkTree(longLived)

total = stretchCheck
for depth = MIN_DEPTH; depth <= MAX_DEPTH; depth = depth + 2:
    iterations = 1 << (MAX_DEPTH - depth + MIN_DEPTH)
    check = 0
    repeat iterations times:
        check = check + checkTree(makeTree(depth))
    total = total + check

total = total + longLivedCheck
```

One line, one integer: `1348958`.

The per-depth checks are:

| depth | iterations | check |
| --- | --- | --- |
| 4 | 8192 | 253952 |
| 6 | 2048 | 260096 |
| 8 | 512 | 261632 |
| 10 | 128 | 262016 |
| 12 | 32 | 262112 |

`stretchCheck` is `32767` (depth 14) and `longLivedCheck` is `16383`
(depth 13). The sum is `1348958`.

## Why this task

It measures recursive allocation, pointer-chasing traversal and reclamation
under a deep, short-lived object graph. It is deliberately not an array or
arena benchmark: each node is an ordinary heap object, and every language uses
its idiomatic node representation with no pooling.

## Language notes

- cpp: `struct Node { Node* left; Node* right; }`, `new`/`delete` or
  `std::unique_ptr`; recursive `makeTree`/`checkTree`.
- rust: `struct Node { left: Option<Box<Node>>, right: Option<Box<Node>> }`.
- go: `type Node struct { left, right *Node }` (a nil child is a leaf).
- js: `class Node { constructor(left, right) { ... } }`, leaf has `null`
  children.
- python: a small `Node` class with `left`/`right` set to `None` for a leaf.
- java: a small `Node` class with `Node left, right`.
- cp3: a `class Node` with `children: Node[]`; a leaf is a node whose
  `children` array is empty, and an internal node always has exactly two
  children. cp3 does not narrow `Node | null` fields, so the children-array
  shape is the supported recursive-tree form. Use a shared
  `const EMPTY: Node[] = [];` for leaves. Use `typeSystem: "rust"` in
  `package.json` for native `i64` loop counters.

No language may reuse or pool nodes: the allocation churn is the workload.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o binary-trees.exe main.cpp
./binary-trees.exe

# rust
rustc -C opt-level=3 -o binary-trees.exe main.rs
./binary-trees.exe

# go
go build -o binary-trees.exe main.go
./binary-trees.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/binary-trees/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/binary-trees/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`binary_trees_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only binary-trees`.
