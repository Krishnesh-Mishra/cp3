"""map-churn (python entry) -- churn a dict of 100,000 integer keys through
delete, reinsert and in-place update for 2 rounds, then fold every key in
ascending order. One integer.

Run:   python main.py

Expected output: 942471769 (see the task README).
"""

KEYS = 100_000
ROUNDS = 2
MOD = 1_000_000_007


def main():
    table = {}
    for i in range(KEYS):
        table[i] = i * 3 + 1

    checksum = 0
    for r in range(ROUNDS):
        for i in range(0, KEYS, 10):
            del table[i]

        for i in range(0, KEYS, 10):
            table[i] = i * 5 + r + 2

        for i in range(0, KEYS, 7):
            table[i] = table[i] + 1

        for i in range(KEYS):
            checksum = (checksum * 31 + table[i]) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
