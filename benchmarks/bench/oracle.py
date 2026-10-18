#!/usr/bin/env python3
# =============================================================================
# benchmarks/bench/oracle.py
#
# Independent reference implementations for every benchmark task's expected
# output. The language entries in benchmarks/<task>/ implement each workload;
# this script implements each one AGAIN, separately, in plain Python, so the
# expected values embedded in bench/run_suite.py and in the task READMEs can
# be checked by anyone. It is never used during timed runs.
#
# Run:  python benchmarks/bench/oracle.py
# =============================================================================
from __future__ import annotations

CHURN_MOD = 1000003
FREQ_MOD = 1000000007
OBJ_MOD = 1000000007
MONO_MOD = 1000000007

# --------------------------------------------------------------------------
# compute-single / compute-single-int / compute-multi
# --------------------------------------------------------------------------
def collatz_total(n: int = 1000000) -> int:
    total = 0
    for start in range(1, n + 1):
        value = start
        steps = 0
        while value != 1:
            if value % 2 == 0:
                value = value // 2
            else:
                value = 3 * value + 1
            steps += 1
        total += steps
    return total


# --------------------------------------------------------------------------
# hello-world
# --------------------------------------------------------------------------
def hello_world_output() -> str:
    return "Hello, world!"


# --------------------------------------------------------------------------
# string-churn
# --------------------------------------------------------------------------
def churn_checksum(iterations: int) -> int:
    seed = "abcXYZ0123456789"
    data = ""
    while len(data) < 4096:
        data += seed
    checksum = 0
    for _ in range(iterations):
        s = data
        for _ in range(8):
            s = s.upper()
            s = s[3:len(s) - 1]
            s = s.lower() + s[0:8]
            s = s[2:]
        checksum = (checksum * 31 + s.find("c") + len(s)) % CHURN_MOD
    return checksum


# --------------------------------------------------------------------------
# word-frequency
# --------------------------------------------------------------------------
def word_frequency_checksum(corpus: int = 3000000, vocab: int = 64) -> int:
    words = [f"word{i:02d}" for i in range(vocab)]
    counts: dict[str, int] = {}
    state = 1
    for _ in range(corpus):
        state = (state * 48271) % 2147483647
        word = words[state % vocab]
        counts[word] = counts.get(word, 0) + 1
    checksum = 0
    for word in words:
        checksum = (checksum * 31 + counts.get(word, 0)) % FREQ_MOD
    return checksum


# --------------------------------------------------------------------------
# object-churn
# --------------------------------------------------------------------------
def object_churn_checksum(objects: int = 5000000) -> int:
    kept: list[int] = []
    acc = 0
    for i in range(objects):
        temp = i                       # Cell(i), dies in scope
        acc = (acc + temp) % OBJ_MOD
        if i % 2 == 0:
            kept.append(i)             # Cell(i), escapes into the array
    checksum = acc
    for value in kept:
        checksum = (checksum * 31 + value) % OBJ_MOD
    return checksum


# --------------------------------------------------------------------------
# array-growth
# --------------------------------------------------------------------------
def array_growth_sum(pushes: int = 20000000) -> int:
    values: list[int] = []
    total = 0
    for i in range(1, pushes + 1):
        values.append(i)
        total += i
    for _ in range(pushes // 2):
        values.pop()
    return total


# --------------------------------------------------------------------------
# closure-capture
# --------------------------------------------------------------------------
def closure_capture_totals(n: int = 10000000, offset: int = 7, cap: int = 3,
                           closures: int = 1000, calls: int = 3000):
    total_map_reduce = 0
    for x in range(n):
        mapped = x + offset            # captured by the map closure
        if mapped % cap == 0:          # captured by the filter closure
            total_map_reduce += mapped # captured by the reduce closure
    total_escaping = 0
    for key in range(closures):
        for _ in range(calls):
            total_escaping += key * key + offset
    return total_map_reduce, total_escaping


# --------------------------------------------------------------------------
# interface-dispatch
# --------------------------------------------------------------------------
def interface_dispatch_totals(count: int = 2000000):
    total_dispatch = 0
    for i in range(count):
        kind = i % 3
        if kind == 0:
            radius = i % 5 + 1
            total_dispatch += 3 * radius * radius      # Circle
        elif kind == 1:
            side = i % 4 + 1
            total_dispatch += side * side              # Square
        else:
            width = i % 3 + 1
            height = i % 6 + 2
            total_dispatch += width * height           # Rect
    total_control = 0
    for i in range(count):
        radius = i % 5 + 1
        total_control += 3 * radius * radius           # direct Circle calls
    return total_dispatch, total_control


# --------------------------------------------------------------------------
# generics-mono
# --------------------------------------------------------------------------
def generics_mono_totals(n: int = 2000000):
    total_generic = 0
    for i in range(n):
        total_generic = (total_generic + i) % MONO_MOD
    total_concrete = 0
    for i in range(n):
        total_concrete = (total_concrete + i) % MONO_MOD
    return total_generic, total_concrete


def main() -> None:
    print("collatz_total(1_000_000)          =", collatz_total())
    print("hello_world_output()              =", repr(hello_world_output()))
    print("churn_checksum(50000)             =", churn_checksum(50000))
    print("churn_checksum(300000)            =", churn_checksum(300000))
    print("word_frequency_checksum()         =", word_frequency_checksum())
    print("object_churn_checksum()           =", object_churn_checksum())
    print("array_growth_sum()                =", array_growth_sum())
    a, b = closure_capture_totals()
    print("closure_capture_totals()          =", a, b)
    a, b = interface_dispatch_totals()
    print("interface_dispatch_totals()       =", a, b)
    a, b = generics_mono_totals()
    print("generics_mono_totals()            =", a, b)


if __name__ == "__main__":
    main()
