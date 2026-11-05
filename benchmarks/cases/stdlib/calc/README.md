# calc - recursive-descent parse + eval

Generate 5000 expressions (seed 12345, xorshift64), every 4th corrupted. Parse and evaluate each; fold valid results. Emit ok_count, err_count, total.
Run: `python benchmarks/runner/run_benchmark.py --only calc`
