"""compute-single-int (python entry) -- total Collatz steps for every start
value in 1..1,000,000, printed as one integer.

Run:   python main.py

Same workload and same output as every other entry. Python int is an exact
arbitrary-precision integer, which is the native-integer choice here; pure
CPython bytecode is still the slowest row, and that is the honest result.
"""


def collatz_steps(start):
    n = start
    steps = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        steps += 1
    return steps


def main():
    total = 0
    for start in range(1, 1_000_000 + 1):
        total += collatz_steps(start)
    print(total)


if __name__ == "__main__":
    main()
