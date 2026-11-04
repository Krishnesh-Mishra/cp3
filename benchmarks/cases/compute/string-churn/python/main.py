"""string-churn (python entry) -- build a 4 KiB string, then repeatedly rebuild
strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.

Run:   python main.py [iterations]        (default 50000)

Same workload and same output in every language entry. Every round allocates
new strings, which is what makes this task exercise allocators and GCs. The
per-iteration fold (checksum * 31 + first 'c' position + length) depends on the
exact string contents, so all six languages must print the same number.
"""

import sys

DEFAULT_ITERATIONS = 50000


def main():
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ITERATIONS

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
        checksum = (checksum * 31 + s.find("c") + len(s)) % 1000003
    print(checksum)


if __name__ == "__main__":
    main()
