"""closure-capture (python entry) -- two closure shapes back to back over a
10,000,000-element source:
  A: map / filter with lambdas that capture offset and cap, summed by the
     builtin reduce (sum)
  B: 1000 closures, each capturing its own key, stored in a list and invoked
     after the creating scope has ended (escaping closures)
Prints both sums on one line.

Run:   python main.py

Same workload and same output in every language entry (expected line in the
task README). The map and filter lambdas capture free variables, so this is a
real closure test in CPython; every element costs a Python-level call, which
is the honest cost of closures in this runtime.
"""

SOURCE = 10_000_000
OFFSET = 7
CAP = 3


def make_closure(key):
    return lambda x: x * key + OFFSET


def main():
    mapped = map(lambda x: x + OFFSET, range(SOURCE))
    filtered = filter(lambda x: x % CAP == 0, mapped)
    total_map_reduce = sum(filtered)

    closures = [make_closure(key) for key in range(1000)]

    total_escaping = 0
    for index, closure in enumerate(closures):
        for _ in range(3000):
            total_escaping += closure(index)

    print(total_map_reduce, total_escaping)


if __name__ == "__main__":
    main()
