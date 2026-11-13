def b99(x):
    acc = x + 99
    acc = (acc * 31 + 693) % 1000033
    acc = (acc * 131 + x * 17 + 99) % 1000199
    return acc
