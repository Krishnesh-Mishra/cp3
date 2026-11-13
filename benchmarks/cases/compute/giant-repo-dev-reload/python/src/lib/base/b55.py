def b55(x):
    acc = x + 55
    acc = (acc * 31 + 385) % 1000033
    acc = (acc * 131 + x * 17 + 55) % 1000199
    return acc
