"""file-big (python entry) -- write and read back an 8 MiB binary payload (the
64-byte seed repeated 131,072 times) for 8 iterations, folding length, byte
sum, first byte and last byte into one integer.

Run:   python main.py

Expected output: 779913 (see the task README).
"""

import os
import tempfile

SIZE = 8_388_608
ITERATIONS = 8
MOD = 1_000_003
SEED = b"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"


def main():
    payload = SEED * (SIZE // len(SEED))
    path = os.path.join(tempfile.gettempdir(), "cp3-bench-file-big-python.bin")

    checksum = 0
    for _ in range(ITERATIONS):
        with open(path, "wb") as handle:
            handle.write(payload)
        with open(path, "rb") as handle:
            data = handle.read()

        checksum = (checksum * 31 + len(data)) % MOD
        checksum = (checksum * 31 + sum(data)) % MOD
        checksum = (checksum * 31 + data[0]) % MOD
        checksum = (checksum * 31 + data[len(data) - 1]) % MOD

    os.remove(path)
    print(checksum)


if __name__ == "__main__":
    main()
