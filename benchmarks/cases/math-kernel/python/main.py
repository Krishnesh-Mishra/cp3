"""math-kernel (python entry) -- evaluate sqrt, floor, abs, pow and clamp over
5,000,000 points, quantize each result to an integer and fold one checksum.

Run:   python main.py

Same workload and same output in every language entry. sqrt is correctly
rounded, pow uses an integer exponent, and every term is quantized to an
integer before folding, so the checksum is exact across libms.
"""

import math

COUNT = 5_000_000
MOD = 1_000_000_007


def main():
    checksum = 0
    sqrt_total = 0.0
    for i in range(1, COUNT + 1):
        x = i * 0.000001
        s = math.sqrt(i)
        sqrt_total += s
        f = math.floor(x + 0.5)
        a = abs(x - 2.5)
        p = math.trunc(math.pow(i, 2))
        c = min(max(x * 1000.0, 0.0), 1000.0)
        mixed = s + f + a + c
        quantum = math.trunc(mixed * 1000000.0)
        checksum = (checksum * 31 + quantum + p) % MOD
    sqrt_scaled = math.trunc(sqrt_total * 1000.0)
    checksum = (checksum * 31 + sqrt_scaled) % MOD
    print(checksum)


if __name__ == "__main__":
    main()
