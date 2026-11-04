"""regex-scan (python entry) -- run a global [0-9]+ scan over a 240,000-byte
generated text for 8 rounds, counting matches and summing their numeric values.
One integer.

Run:   python main.py

Expected output: 735703090 (see the task README).
"""

import re

UNIT = "alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n"
REPEATS = 5000
ROUNDS = 8
MOD = 1_000_000_007


def main():
    text = UNIT * REPEATS
    checksum = 0

    for _ in range(ROUNDS):
        matches = re.findall(r"[0-9]+", text)
        count = len(matches)
        total = 0
        for match in matches:
            total += int(match)
        checksum = (checksum * 31 + count + total) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
