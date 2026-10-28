"""generics-mono (python entry) -- the same 2,000,000-iteration sum run twice:
once through a box class plus a passthrough function, once through a
hand-written second class. Prints both sums on one line.

Run:   python main.py

Same workload and same output in every language entry (expected line in the
task README). Python has no type parameters: both loops are plain classes and
attribute lookup plus instance creation dominate, so the ratio between the two
loops is expected to hover at 1.0 for this row; that is the honest reading for
a language where monomorphization does not exist.
"""

ITERATIONS = 2_000_000
MOD = 1_000_000_007


class Box:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


def sum_box(box):
    return box.get()


class NumberBox:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


def main():
    total_generic = 0
    for i in range(ITERATIONS):
        box = Box(i)
        total_generic = (total_generic + sum_box(box)) % MOD

    total_concrete = 0
    for i in range(ITERATIONS):
        box = NumberBox(i)
        total_concrete = (total_concrete + box.get()) % MOD

    print(total_generic, total_concrete)


if __name__ == "__main__":
    main()
