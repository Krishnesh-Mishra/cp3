def b2(x):
    acc = x + 2
    acc = (acc * 31 + 14) % 1000033
    acc = (acc * 131 + x * 17 + 2) % 1000199
    return acc
