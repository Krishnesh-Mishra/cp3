def b32(x):
    acc = x + 32
    acc = (acc * 31 + 224) % 1000033
    acc = (acc * 131 + x * 17 + 32) % 1000199
    return acc
