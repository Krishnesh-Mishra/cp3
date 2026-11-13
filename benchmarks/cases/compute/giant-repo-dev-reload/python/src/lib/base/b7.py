def b7(x):
    acc = x + 7
    acc = (acc * 31 + 49) % 1000033
    acc = (acc * 131 + x * 17 + 7) % 1000199
    return acc
