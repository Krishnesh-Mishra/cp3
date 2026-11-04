# tree-walk - pointer-linked binary tree

Binary tree with value per node, built once, summed 8 passes with pass index folded in.

Constants: DEPTH = 13, seed = 12345, xorshift64, value = xs_mod(seed, 1000).
Counts: `count = 2^(DEPTH+1)-1` then `total`.
Run: `python benchmarks/runner/run_benchmark.py --only tree-walk`
