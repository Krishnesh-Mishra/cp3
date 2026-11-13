def b8(x):
    acc = x + 8
    acc = (acc * 31 + 56) % 1000033
    acc = (acc * 131 + x * 17 + 8) % 1000199
    return acc
