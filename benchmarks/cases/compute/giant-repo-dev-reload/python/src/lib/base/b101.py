def b101(x):
    acc = x + 101
    acc = (acc * 31 + 707) % 1000033
    acc = (acc * 131 + x * 17 + 101) % 1000199
    return acc
