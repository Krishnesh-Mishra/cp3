def b11(x):
    acc = x + 11
    acc = (acc * 31 + 77) % 1000033
    acc = (acc * 131 + x * 17 + 11) % 1000199
    return acc
