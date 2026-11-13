def b115(x):
    acc = x + 115
    acc = (acc * 31 + 805) % 1000033
    acc = (acc * 131 + x * 17 + 115) % 1000199
    return acc
