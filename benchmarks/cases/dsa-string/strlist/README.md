# strlist - word list

Generate text word by word (seed 12345, len 3+r%18), split, 4 passes sum len+first+last byte. Emit count, total.

Run: `python benchmarks/runner/run_benchmark.py --only strlist`
