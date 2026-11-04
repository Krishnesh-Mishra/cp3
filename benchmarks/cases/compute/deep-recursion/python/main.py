"""deep-recursion (python entry) -- recurse to depth 5,000 for 200 rounds,
returning the triangular sum each time, and fold it into one integer.

Run:   python main.py

Expected output: 292249866 (see the task README).
"""

import sys

sys.setrecursionlimit(20_000)

DEPTH = 5000
ROUNDS = 200
MOD = 1_000_000_007


def sum_to(n):
    if n == 0:
        return 0
    return n + sum_to(n - 1)


def main():
    checksum = 0
    for _ in range(ROUNDS):
        checksum = (checksum * 31 + sum_to(DEPTH)) % MOD
    print(checksum)


if __name__ == "__main__":
    main()
