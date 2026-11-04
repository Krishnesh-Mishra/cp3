"""compute-multi (python entry) -- the compute-single workload split across
os.cpu_count() worker processes, each summing a contiguous chunk of
1..1,000,000; the partials are combined on the main process.

Run:   python main.py

Prints the same single integer as every other entry (single or multi), so the
outputs cross-check each other.

GIL note: this uses concurrent.futures.ProcessPoolExecutor, i.e. real OS
processes, not threads -- CPython's GIL serializes bytecode execution, so a
ThreadPoolExecutor would give no speedup for CPU-bound work. The price is
startup: on Windows every worker is spawned (not forked) and re-imports this
module, so process-spawn cost is included in the wall time measured from
outside.
"""

import os
from concurrent.futures import ProcessPoolExecutor


def collatz_steps(start):
    n = start
    steps = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        steps += 1
    return steps


def sum_range(bounds):
    first, last = bounds
    total = 0
    for start in range(first, last + 1):
        total += collatz_steps(start)
    return total


def main():
    n = 1_000_000
    workers = os.cpu_count() or 1
    chunk = (n + workers - 1) // workers

    chunks = []
    for w in range(workers):
        first = w * chunk + 1
        last = min(n, (w + 1) * chunk)
        if first > n:
            break
        chunks.append((first, last))

    with ProcessPoolExecutor(max_workers=workers) as pool:
        total = sum(pool.map(sum_range, chunks))
    print(total)


if __name__ == "__main__":
    main()
