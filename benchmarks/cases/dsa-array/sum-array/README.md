# sum-array - flat scalar scan (control)

Flat array of N scalars scanned PASSES times. Nothing plays to a DSA
strength; a dead heat here is the evidence that the harness measures the
algorithm, not the toolchain.

Constants: N = 4_000_000, PASSES = 8, seed = 12345, xorshift64
(`xs_next`), each value = `xs_mod(r, 1000)`. Checksum: for each pass p,
sum `value ^ p`.

Expected stdout (2 lines): `N` then `total`.
Run: `python benchmarks/runner/run_benchmark.py --only sum-array`
