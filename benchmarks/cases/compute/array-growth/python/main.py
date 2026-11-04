"""array-growth (python entry) -- push 20,000,000 numbers into a growing list
while summing them, pop half of them back off, then print the sum.

Run:   python main.py

Same workload and same output in every language entry (expected value in the
task README). The list starts empty so over-allocation growth is part of the
workload; int is exact arbitrary precision, so the sum is exact by definition.
"""

PUSHES = 20_000_000


def main():
    values = []
    total = 0
    for i in range(1, PUSHES + 1):
        values.append(i)
        total += i
    for _ in range(PUSHES // 2):
        values.pop()
    print(total)


if __name__ == "__main__":
    main()
