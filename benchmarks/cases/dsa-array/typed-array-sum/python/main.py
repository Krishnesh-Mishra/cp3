"""typed-array-sum (python entry) -- fill a fixed Float64Array and Uint8Array,
sum the floats for 32 rounds, then fold the float and byte totals into one
integer checksum.

Run:   python main.py

Same workload and same output in every language entry. The float pattern is a
multiple of 0.25 and every partial sum stays far below 2^53, so the addition is
exact in IEEE-754 doubles. Python uses the stdlib typed-array shapes:
array('d') for the doubles and bytearray for the bytes.
"""

from array import array
import math

SIZE = 1_000_000
ROUNDS = 32
MOD = 1_000_000_007


def main():
    values = array("d", [0.0]) * SIZE
    data = bytearray(SIZE)
    for i in range(SIZE):
        values[i] = (i % 97) * 0.5 + (i % 13) * 0.25
        data[i] = (i * 31 + 7) % 256

    total = 0.0
    for _ in range(ROUNDS):
        acc = 0.0
        for i in range(SIZE):
            acc += values[i]
        total += acc

    byte_total = sum(data)
    scaled = math.trunc(total * 4.0)
    checksum = (scaled + byte_total) % MOD
    checksum = (checksum * 31 + SIZE + ROUNDS) % MOD
    print(checksum)


if __name__ == "__main__":
    main()
