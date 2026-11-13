def b34(x):
    acc = x + 34
    acc = (acc * 31 + 238) % 1000033
    acc = (acc * 131 + x * 17 + 34) % 1000199
    return acc
