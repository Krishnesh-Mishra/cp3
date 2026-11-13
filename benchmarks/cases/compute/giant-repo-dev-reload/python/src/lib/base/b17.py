def b17(x):
    acc = x + 17
    acc = (acc * 31 + 119) % 1000033
    acc = (acc * 131 + x * 17 + 17) % 1000199
    return acc
