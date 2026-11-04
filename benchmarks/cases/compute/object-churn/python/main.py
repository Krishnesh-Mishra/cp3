"""object-churn (python entry) -- create 5,000,000 small objects in a hot loop:
each iteration's temporary dies in scope, and every second object escapes into
a list. One checksum at the end.

Run:   python main.py

Same workload and same output in every language entry (expected value in the
task README). __slots__ keeps the per-object cost down; instance creation and
__init__ dispatch are pure CPython bytecode here, which is the honest result.
"""

OBJECTS = 5_000_000
MOD = 1_000_000_007


class Cell:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


def main():
    kept = []
    acc = 0
    for i in range(OBJECTS):
        temp = Cell(i)
        acc = (acc + temp.value) % MOD
        if i % 2 == 0:
            kept.append(Cell(i))

    checksum = acc
    for cell in kept:
        checksum = (checksum * 31 + cell.value) % MOD
    print(checksum)


if __name__ == "__main__":
    main()
