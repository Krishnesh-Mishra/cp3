"""exception-cost (python entry) -- run 1,000,000 predicate calls through a
raise/except ValueError path and the same calls through a -1 sentinel return,
folding both into checksums printed on one line.

Run:   python main.py

Expected output: 446742259 446742259 (see the task README).
"""

ITERATIONS = 1_000_000
MOD = 1_000_000_007


def may_throw(i):
    if i % 3 == 0:
        raise ValueError("boom")
    return i


def may_return(i):
    if i % 3 == 0:
        return -1
    return i


def main():
    thrown = 0
    for i in range(ITERATIONS):
        try:
            value = may_throw(i)
            thrown = (thrown * 31 + value) % MOD
        except ValueError:
            thrown = (thrown * 31 + 1) % MOD

    returned = 0
    for i in range(ITERATIONS):
        value = may_return(i)
        if value < 0:
            returned = (returned * 31 + 1) % MOD
        else:
            returned = (returned * 31 + value) % MOD

    print(thrown, returned)


if __name__ == "__main__":
    main()
