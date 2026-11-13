def b23(x):
    acc = x + 23
    acc = (acc * 31 + 161) % 1000033
    acc = (acc * 131 + x * 17 + 23) % 1000199
    return acc
