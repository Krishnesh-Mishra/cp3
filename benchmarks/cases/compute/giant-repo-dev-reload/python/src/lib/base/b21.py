def b21(x):
    acc = x + 21
    acc = (acc * 31 + 147) % 1000033
    acc = (acc * 131 + x * 17 + 21) % 1000199
    return acc
