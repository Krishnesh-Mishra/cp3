def b89(x):
    acc = x + 89
    acc = (acc * 31 + 623) % 1000033
    acc = (acc * 131 + x * 17 + 89) % 1000199
    return acc
