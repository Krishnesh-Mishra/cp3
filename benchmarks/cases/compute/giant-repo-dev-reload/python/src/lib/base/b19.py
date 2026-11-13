def b19(x):
    acc = x + 19
    acc = (acc * 31 + 133) % 1000033
    acc = (acc * 131 + x * 17 + 19) % 1000199
    return acc
