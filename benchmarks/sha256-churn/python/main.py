"""sha256-churn (python entry) -- hash one fixed 1 KiB buffer ITERATIONS times
and fold the first four digest bytes into a checksum.

Run:    python main.py

Same workload and same output as the cp3, Go and JavaScript entries; the C++
and Rust entries are omitted because neither standard library ships SHA-256
(see TASK_OMIT_LANGUAGES in bench/run_suite.py).

The buffer is the seed repeated to exactly 1024 bytes and never changes, so
every hash is over identical bytes; what the task measures is the hash
implementation plus the per-call boundary. hashlib returns the digest as 32
bytes; the first four are read as one big-endian 32-bit integer.
"""

import hashlib

BUFFER_SIZE = 1024
ITERATIONS = 60000
SEED = b"0123456789abcdef"


def sha256_churn(iterations):
    data = (SEED * (BUFFER_SIZE // len(SEED)))[:BUFFER_SIZE]
    checksum = 0
    for _ in range(iterations):
        digest = hashlib.sha256(data).digest()
        first_four_bytes = int.from_bytes(digest[:4], "big")
        checksum = (checksum * 31 + first_four_bytes) % 1000003
    return checksum


print(sha256_churn(ITERATIONS))
