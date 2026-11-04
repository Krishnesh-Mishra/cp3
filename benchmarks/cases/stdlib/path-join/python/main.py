"""path-join (python entry) -- join, normalize, basename and extname one fixed
path-segment list N times and fold stable string lengths and characters.

Run:   python main.py

Same workload and same output as the cp3, C++, Rust, Go and JavaScript entries.
Only lengths and the first character of each result are folded, so the checksum
is identical on Windows and POSIX (the separator is one character in every
language's path surface).
"""

import os

ITERATIONS = 170000
MOD = 1000003
PARTS = ["var", "log", "cp3", "service", "events-2026-10-23.log"]


def main() -> None:
    checksum = 0
    for _ in range(ITERATIONS):
        joined = os.path.join(*PARTS)
        normalized = os.path.normpath(joined)
        base = os.path.basename(joined)
        ext = os.path.splitext(joined)[1]

        checksum = (checksum * 31 + len(joined)) % MOD
        checksum = (checksum * 31 + len(normalized)) % MOD
        checksum = (checksum * 31 + len(base)) % MOD
        checksum = (checksum * 31 + len(ext)) % MOD
        checksum = (checksum * 31 + ord(joined[0])) % MOD
        checksum = (checksum * 31 + ord(base[0])) % MOD
        checksum = (checksum * 31 + ord(ext[0])) % MOD
    print(checksum)


if __name__ == "__main__":
    main()
