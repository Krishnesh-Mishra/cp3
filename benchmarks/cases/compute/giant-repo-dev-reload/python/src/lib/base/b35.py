def b35(x):
    acc = x + 35
    acc = (acc * 31 + 245) % 1000033
    acc = (acc * 131 + x * 17 + 35) % 1000199
    return acc
