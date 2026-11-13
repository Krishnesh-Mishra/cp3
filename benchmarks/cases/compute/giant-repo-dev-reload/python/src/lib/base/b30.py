def b30(x):
    acc = x + 30
    acc = (acc * 31 + 210) % 1000033
    acc = (acc * 131 + x * 17 + 30) % 1000199
    return acc
