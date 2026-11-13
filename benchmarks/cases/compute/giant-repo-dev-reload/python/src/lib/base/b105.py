def b105(x):
    acc = x + 105
    acc = (acc * 31 + 735) % 1000033
    acc = (acc * 131 + x * 17 + 105) % 1000199
    return acc
