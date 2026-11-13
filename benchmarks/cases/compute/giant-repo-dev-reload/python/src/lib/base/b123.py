def b123(x):
    acc = x + 123
    acc = (acc * 31 + 861) % 1000033
    acc = (acc * 131 + x * 17 + 123) % 1000199
    return acc
