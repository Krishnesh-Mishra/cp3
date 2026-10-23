"""file-roundtrip (python entry) -- write, read and stat one fixed 4 KiB file
N times in the OS temp directory, then remove it.

Run:   python main.py

Same workload and same output as the cp3, C++, Rust, Go and JavaScript entries.
The checksum folds the bytes read (length, byte sum, first and last byte) and
the stat result (size, isFile, isDirectory), so a language that wrote a
different file or mis-stated the metadata would print a different number.
"""

import os
import stat
import tempfile

ITERATIONS = 200
FILE_BYTES = 4096
MOD = 1000003


def payload() -> bytes:
    seed = b"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    data = b""
    while len(data) < FILE_BYTES:
        data += seed
    return data[:FILE_BYTES]


def main() -> None:
    data = payload()
    file = os.path.join(tempfile.gettempdir(), "cp3-bench-file-roundtrip.txt")

    checksum = 0
    for _ in range(ITERATIONS):
        with open(file, "wb") as out:
            out.write(data)
        with open(file, "rb") as source:
            read = source.read()
        if len(read) != FILE_BYTES:
            raise RuntimeError("wrong file size")
        info = os.stat(file)

        checksum = (checksum * 31 + len(read)) % MOD
        checksum = (checksum * 31 + sum(read)) % MOD
        checksum = (checksum * 31 + read[0]) % MOD
        checksum = (checksum * 31 + read[-1]) % MOD
        checksum = (checksum * 31 + info.st_size) % MOD
        checksum = (checksum * 31 + (1 if stat.S_ISREG(info.st_mode) else 0)) % MOD
        checksum = (checksum * 31 + (1 if stat.S_ISDIR(info.st_mode) else 0)) % MOD

    os.remove(file)
    print(checksum)


if __name__ == "__main__":
    main()
