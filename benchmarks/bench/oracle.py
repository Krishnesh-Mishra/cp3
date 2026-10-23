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

import datetime
import hashlib
import json
import os
import socket
import sys
import tempfile

CHURN_MOD = 1000003
FREQ_MOD = 1000000007
OBJ_MOD = 1000000007
MONO_MOD = 1000000007
JSON_MOD = 1000003
ASYNC_MOD = 1000003
SHA256_MOD = 1000003
BUFFER_MOD = 1000003
ENV_MOD = 1000003
FS_MOD = 1000003
PATH_MOD = 1000003
DATE_MOD = 1000003
OS_MOD = 1000003

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
# sha256-churn
# --------------------------------------------------------------------------
# One fixed 1 KiB buffer, hashed ITERATIONS times. The checksum folds the
# first four digest bytes as one big-endian 32-bit integer, which is what the
# cp3 hex-first-eight-digits parse, Go's binary.BigEndian.Uint32, JS's
# parseInt of the first eight hex digits and Python's int.from_bytes all read.
SHA256_ITERATIONS = 60000
SHA256_BUFFER_SIZE = 1024
SHA256_SEED = b"0123456789abcdef"


def sha256_churn_checksum(iterations: int = SHA256_ITERATIONS,
                          buffer_size: int = SHA256_BUFFER_SIZE) -> int:
    data = (SHA256_SEED * (buffer_size // len(SHA256_SEED)))[:buffer_size]
    checksum = 0
    for _ in range(iterations):
        digest = hashlib.sha256(data).digest()
        first_four_bytes = int.from_bytes(digest[:4], "big")
        checksum = (checksum * 31 + first_four_bytes) % SHA256_MOD
    return checksum


# --------------------------------------------------------------------------
# buffer-copy
# --------------------------------------------------------------------------
# A 64 KiB buffer filled with the repeated 64-byte seed is reshaped
# ITERATIONS times: rotate left by a changing offset, overwrite the new first
# byte with a cycling marker, then fold the first WINDOW bytes (each marker
# position + 1, so "absent" folds as 0 and the value is never negative).
BUFFER_ITERATIONS = 5000
BUFFER_SIZE = 65536
BUFFER_WINDOW = 96
BUFFER_SEED = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?"


def buffer_copy_checksum(iterations: int = BUFFER_ITERATIONS,
                         size: int = BUFFER_SIZE,
                         window: int = BUFFER_WINDOW) -> int:
    buf = bytearray(BUFFER_SEED * (size // len(BUFFER_SEED)))
    checksum = 0
    for k in range(iterations):
        offset = (k * 4093 + 17) % size
        marker = BUFFER_SEED[k % len(BUFFER_SEED)]
        buf = bytearray((marker,)) + buf[offset + 1:] + buf[:offset]
        first_a = buf.find(b"a", 0, window) + 1
        first_7 = buf.find(b"7", 0, window) + 1
        checksum = (checksum * 131 + len(buf) + first_a + first_7) % BUFFER_MOD
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
# env-injection
# --------------------------------------------------------------------------
ENV_ITERATIONS = 500000
ENV_VAR = "CP3_BENCH_ENV"
ENV_VALUE = "cp3-benchmark-value"


def env_injection_checksum(iterations: int = ENV_ITERATIONS,
                           value: str = ENV_VALUE) -> int:
    data = value.encode("utf-8")
    checksum = 0
    for _ in range(iterations):
        checksum = (checksum * 31 + len(data)) % ENV_MOD
        checksum = (checksum * 31 + sum(data)) % ENV_MOD
        checksum = (checksum * 31 + data[0]) % ENV_MOD
        checksum = (checksum * 31 + data[-1]) % ENV_MOD
    return checksum


# --------------------------------------------------------------------------
# file-roundtrip
# --------------------------------------------------------------------------
FS_ITERATIONS = 200
FS_BYTES = 4096
FS_SEED = b"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"


def file_roundtrip_checksum(iterations: int = FS_ITERATIONS,
                            size: int = FS_BYTES) -> int:
    data = (FS_SEED * (size // len(FS_SEED) + 1))[:size]
    checksum = 0
    for _ in range(iterations):
        checksum = (checksum * 31 + len(data)) % FS_MOD
        checksum = (checksum * 31 + sum(data)) % FS_MOD
        checksum = (checksum * 31 + data[0]) % FS_MOD
        checksum = (checksum * 31 + data[-1]) % FS_MOD
        checksum = (checksum * 31 + len(data)) % FS_MOD      # stat size
        checksum = (checksum * 31 + 1) % FS_MOD              # isFile
        checksum = (checksum * 31 + 0) % FS_MOD              # isDirectory
    return checksum


# --------------------------------------------------------------------------
# path-join
# --------------------------------------------------------------------------
PATH_ITERATIONS = 170000
PATH_PARTS = ["var", "log", "cp3", "service", "events-2026-10-23.log"]


def path_join_checksum(iterations: int = PATH_ITERATIONS) -> int:
    # The reference spelling is POSIX-style: the fixed parts contain no
    # separators, so join/normalize differ only in that one separator char,
    # which this fold never reads.
    joined = "/".join(PATH_PARTS)
    normalized = joined
    base = PATH_PARTS[-1]
    ext = "." + base.rsplit(".", 1)[1] if "." in base else ""
    checksum = 0
    for _ in range(iterations):
        checksum = (checksum * 31 + len(joined)) % PATH_MOD
        checksum = (checksum * 31 + len(normalized)) % PATH_MOD
        checksum = (checksum * 31 + len(base)) % PATH_MOD
        checksum = (checksum * 31 + len(ext)) % PATH_MOD
        checksum = (checksum * 31 + ord(joined[0])) % PATH_MOD
        checksum = (checksum * 31 + ord(base[0])) % PATH_MOD
        checksum = (checksum * 31 + ord(ext[0])) % PATH_MOD
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


# --------------------------------------------------------------------------
# date-format
# --------------------------------------------------------------------------
# The two fixed instants (the second one is exactly 123,456 ms after the
# first), the per-iteration second offset and the shared character weights.
DATE_BASE_MS = 1700000000000       # 2023-11-14T22:13:20Z
DATE_LATER_MS = 1700000123456      # 2023-11-14T22:15:23.456Z, +123,456 ms
DATE_ITERATIONS = 50000
DATE_OFFSETS = 3600                # seconds in one hour
CHAR_VALUES = {str(digit): digit for digit in range(10)}
CHAR_VALUES.update({"-": 10, "T": 11, ":": 12})


def date_format_checksum(iterations: int = DATE_ITERATIONS) -> int:
    checksum = 0
    diff_ms = DATE_LATER_MS - DATE_BASE_MS
    for k in range(iterations):
        instant_ms = DATE_BASE_MS + (k % DATE_OFFSETS) * 1000
        # Whole seconds only: every instant in this workload is second-aligned.
        moment = datetime.datetime.fromtimestamp(instant_ms // 1000,
                                                 datetime.timezone.utc)
        text = (f"{moment.year:04d}-{moment.month:02d}-{moment.day:02d}"
                f"T{moment.hour:02d}:{moment.minute:02d}:{moment.second:02d}")
        char_sum = sum(CHAR_VALUES[ch] for ch in text)
        checksum = (checksum * 31 + char_sum + len(text) + diff_ms) % DATE_MOD
    return checksum


# --------------------------------------------------------------------------
# os-probe
# --------------------------------------------------------------------------
# Platform names map to 1/2/3, then reduce to a "documented platform" boolean;
# CPU count and the home/temp/hostname strings reduce to non-empty booleans.
# Only those booleans are folded, so the checksum is identical on any machine
# (and every supported OS) instead of encoding this host's paths or core count.
OS_ITERATIONS = 50000
OS_PLATFORM_CODES = {"win32": 1, "darwin": 2, "linux": 3}


def os_probe_checksum(iterations: int = OS_ITERATIONS) -> int:
    checksum = 0
    for _ in range(iterations):
        code = OS_PLATFORM_CODES.get(sys.platform, 0)
        platform_ok = 1 if 1 <= code <= 3 else 0
        cpu_ok = 1 if (os.cpu_count() or 0) > 0 else 0
        home_ok = 1 if os.path.expanduser("~") else 0
        tmp_ok = 1 if tempfile.gettempdir() else 0
        host_ok = 1 if socket.gethostname() else 0
        term = (platform_ok * 16 + cpu_ok * 8 + home_ok * 4 +
                tmp_ok * 2 + host_ok)
        checksum = (checksum * 31 + term) % OS_MOD
    return checksum


def main() -> None:
    print("collatz_total(1_000_000)          =", collatz_total())
    print("hello_world_output()              =", repr(hello_world_output()))
    print("churn_checksum(50000)             =", churn_checksum(50000))
    print("churn_checksum(300000)            =", churn_checksum(300000))
    print("sha256_churn_checksum()           =", sha256_churn_checksum())
    print("buffer_copy_checksum()            =", buffer_copy_checksum())
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
    print("env_injection_checksum()          =", env_injection_checksum())
    print("file_roundtrip_checksum()         =", file_roundtrip_checksum())
    print("path_join_checksum()              =", path_join_checksum())
    print("date_format_checksum()            =", date_format_checksum())
    print("os_probe_checksum()               =", os_probe_checksum())


if __name__ == "__main__":
    main()
