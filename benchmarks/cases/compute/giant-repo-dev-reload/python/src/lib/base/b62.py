def b62(x):
    acc = x + 62
    acc = (acc * 31 + 434) % 1000033
    acc = (acc * 131 + x * 17 + 62) % 1000199
    return acc
