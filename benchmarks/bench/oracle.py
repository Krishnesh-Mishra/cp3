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

from array import array
import json
import math

CHURN_MOD = 1000003
FREQ_MOD = 1000000007
OBJ_MOD = 1000000007
MONO_MOD = 1000000007
JSON_MOD = 1000003
ASYNC_MOD = 1000003

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
# typed-array-sum
# --------------------------------------------------------------------------
TA_SIZE = 1000000
TA_ROUNDS = 32
TA_MOD = 1000000007


def typed_array_sum_checksum(size: int = TA_SIZE, rounds: int = TA_ROUNDS) -> int:
    values = array("d", [0.0]) * size
    data = bytearray(size)
    for i in range(size):
        values[i] = (i % 97) * 0.5 + (i % 13) * 0.25
        data[i] = (i * 31 + 7) % 256

    total = 0.0
    for _ in range(rounds):
        acc = 0.0
        for i in range(size):
            acc += values[i]
        total += acc

    scaled = math.trunc(total * 4.0)
    checksum = (scaled + sum(data)) % TA_MOD
    checksum = (checksum * 31 + size + rounds) % TA_MOD
    return checksum


# --------------------------------------------------------------------------
# math-kernel
# --------------------------------------------------------------------------
MK_COUNT = 5000000
MK_MOD = 1000000007


def math_kernel_checksum(count: int = MK_COUNT) -> int:
    checksum = 0
    sqrt_total = 0.0
    for i in range(1, count + 1):
        x = i * 0.000001
        s = math.sqrt(i)
        sqrt_total += s
        f = math.floor(x + 0.5)
        a = abs(x - 2.5)
        p = math.trunc(math.pow(i, 2))
        c = min(max(x * 1000.0, 0.0), 1000.0)
        mixed = s + f + a + c
        quantum = math.trunc(mixed * 1000000.0)
        checksum = (checksum * 31 + quantum + p) % MK_MOD
    checksum = (checksum * 31 + math.trunc(sqrt_total * 1000.0)) % MK_MOD
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


# --------------------------------------------------------------------------
# json-roundtrip
# --------------------------------------------------------------------------
# The literal JSON text every entry embeds verbatim (one representative API
# record: scalars, strings, a boolean, a fractional score and two primitive
# arrays). Kept ASCII-only on purpose: every entry folds lengths, and ASCII is
# the one text domain where JS/Python character counts, Go/cp3 byte counts and
# UTF-16 code units agree exactly.
JSON_ITERATIONS = 20000
JSON_PAYLOAD = (
    '{"id":48271,"name":"Ada Lovelace","email":"ada.lovelace@example.com",'
    '"active":true,"score":93.5,"title":"Principal Engineer",'
    '"company":"Analytical Engines Ltd","city":"London","country":"GB",'
    '"tags":["analyst","engineer","pioneer","mathematician","compiler",'
    '"runtime"],"samples":[12,17,23,29,31,37,41,43,47,53]}'
)


def json_roundtrip_checksum(iterations: int = JSON_ITERATIONS) -> int:
    checksum = 0
    for _ in range(iterations):
        u = json.loads(JSON_PAYLOAD)              # the parse half
        out = json.dumps(                         # the stringify half
            {
                "id": u["id"],
                "name": u["name"],
                "email": u["email"],
                "active": u["active"],
                "score": u["score"],
                "title": u["title"],
                "company": u["company"],
                "city": u["city"],
                "country": u["country"],
                "tags": u["tags"],
                "samples": u["samples"],
            },
            separators=(",", ":"),
            ensure_ascii=True,
        )
        tags = u["tags"]
        samples = u["samples"]
        field_value = (
            u["id"]
            + len(u["name"])
            + len(u["email"])
            + (1 if u["active"] else 0)
            + int(u["score"] * 10)
            + len(u["title"])
            + len(u["company"])
            + len(u["city"])
            + len(u["country"])
            + len(tags)
            + len(samples)
            + sum(len(tag) for tag in tags)
            + sum(samples)
        )
        checksum = (checksum * 31 + field_value + len(JSON_PAYLOAD) + len(out)) % JSON_MOD
    return checksum


def json_parse_only_checksum(iterations: int = JSON_ITERATIONS) -> int:
    """Diagnostic probe: the parse half alone (no stringify).

    Same field fold as json_roundtrip_checksum, minus the stringify length, so
    a parse-only entry can be checked against an independent value.
    """
    u = json.loads(JSON_PAYLOAD)
    tags = u["tags"]
    samples = u["samples"]
    field_value = (
        u["id"]
        + len(u["name"])
        + len(u["email"])
        + (1 if u["active"] else 0)
        + int(u["score"] * 10)
        + len(u["title"])
        + len(u["company"])
        + len(u["city"])
        + len(u["country"])
        + len(tags)
        + len(samples)
        + sum(len(tag) for tag in tags)
        + sum(samples)
    )
    checksum = 0
    for _ in range(iterations):
        checksum = (checksum * 31 + field_value + len(JSON_PAYLOAD)) % JSON_MOD
    return checksum


def json_stringify_only_checksum(iterations: int = JSON_ITERATIONS) -> int:
    """Diagnostic probe: the stringify half alone (fold the output length)."""
    u = json.loads(JSON_PAYLOAD)
    out = json.dumps(
        {
            "id": u["id"],
            "name": u["name"],
            "email": u["email"],
            "active": u["active"],
            "score": u["score"],
            "title": u["title"],
            "company": u["company"],
            "city": u["city"],
            "country": u["country"],
            "tags": u["tags"],
            "samples": u["samples"],
        },
        separators=(",", ":"),
        ensure_ascii=True,
    )
    checksum = 0
    for _ in range(iterations):
        checksum = (checksum * 31 + len(out)) % JSON_MOD
    return checksum


# --------------------------------------------------------------------------
# async-fanout
# --------------------------------------------------------------------------
# Phase 1: 10,000 timers release 0..9,999, joined at once -> their sum.
# Phase 2: 10,000 sequential awaits of an already-settled value, each folding
# the loop index. Pure arithmetic: the point of the oracle here is the fold
# order, not a re-implementation of the event loop.
ASYNC_FANOUT = 10000
ASYNC_CHAIN = 10000


def async_fanout_checksum(fanout: int = ASYNC_FANOUT, chain: int = ASYNC_CHAIN) -> int:
    checksum = (fanout * (fanout - 1) // 2) % ASYNC_MOD
    for i in range(chain):
        checksum = (checksum * 31 + i) % ASYNC_MOD
    return checksum


def main() -> None:
    print("collatz_total(1_000_000)          =", collatz_total())
    print("hello_world_output()              =", repr(hello_world_output()))
    print("churn_checksum(50000)             =", churn_checksum(50000))
    print("churn_checksum(300000)            =", churn_checksum(300000))
    print("typed_array_sum_checksum()        =", typed_array_sum_checksum())
    print("math_kernel_checksum()            =", math_kernel_checksum())
    print("word_frequency_checksum()         =", word_frequency_checksum())
    print("object_churn_checksum()           =", object_churn_checksum())
    print("array_growth_sum()                =", array_growth_sum())
    a, b = closure_capture_totals()
    print("closure_capture_totals()          =", a, b)
    a, b = interface_dispatch_totals()
    print("interface_dispatch_totals()       =", a, b)
    a, b = generics_mono_totals()
    print("generics_mono_totals()            =", a, b)
    print("json payload bytes                =", len(JSON_PAYLOAD))
    print("json_roundtrip_checksum()         =", json_roundtrip_checksum())
    print("json_parse_only_checksum()        =", json_parse_only_checksum())
    print("json_stringify_only_checksum()    =", json_stringify_only_checksum())
    print("async_fanout_checksum()           =", async_fanout_checksum())


if __name__ == "__main__":
    main()
