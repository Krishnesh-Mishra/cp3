def b47(x):
    acc = x + 47
    acc = (acc * 31 + 329) % 1000033
    acc = (acc * 131 + x * 17 + 47) % 1000199
    return acc
