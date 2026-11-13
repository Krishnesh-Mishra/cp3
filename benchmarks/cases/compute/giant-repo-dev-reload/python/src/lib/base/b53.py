def b53(x):
    acc = x + 53
    acc = (acc * 31 + 371) % 1000033
    acc = (acc * 131 + x * 17 + 53) % 1000199
    return acc
