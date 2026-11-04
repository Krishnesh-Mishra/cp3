"""env-injection (python entry) -- read one injected environment variable N
times and fold its bytes into a checksum.

Run:   python main.py        (CP3_BENCH_ENV must be set)

The suite runner injects CP3_BENCH_ENV into every child process for this task.
Each iteration performs a fresh os.environ read (the lookup itself is the
workload) and folds the value's length, byte sum, first byte and last byte into
the checksum. A missing variable is an error, so a run without the injection
fails instead of measuring an empty string.

Same workload and same output as the cp3, C++, Rust, Go and JavaScript entries.
"""

import os
import sys

ITERATIONS = 500000
KEY = "CP3_BENCH_ENV"
MOD = 1000003


def main():
    checksum = 0
    for _ in range(ITERATIONS):
        try:
            value = os.environ[KEY]
        except KeyError:
            print("CP3_BENCH_ENV is not set", file=sys.stderr)
            return 1
        data = value.encode("utf-8")
        checksum = (checksum * 31 + len(data)) % MOD
        checksum = (checksum * 31 + sum(data)) % MOD
        checksum = (checksum * 31 + data[0]) % MOD
        checksum = (checksum * 31 + data[-1]) % MOD
    print(checksum)
    return 0


if __name__ == "__main__":
    sys.exit(main())
