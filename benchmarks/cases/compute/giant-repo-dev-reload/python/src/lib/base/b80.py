def b80(x):
    acc = x + 80
    acc = (acc * 31 + 560) % 1000033
    acc = (acc * 131 + x * 17 + 80) % 1000199
    return acc
