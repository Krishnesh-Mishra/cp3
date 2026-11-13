def b77(x):
    acc = x + 77
    acc = (acc * 31 + 539) % 1000033
    acc = (acc * 131 + x * 17 + 77) % 1000199
    return acc
