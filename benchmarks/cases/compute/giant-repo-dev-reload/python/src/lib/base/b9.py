def b9(x):
    acc = x + 9
    acc = (acc * 31 + 63) % 1000033
    acc = (acc * 131 + x * 17 + 9) % 1000199
    return acc
