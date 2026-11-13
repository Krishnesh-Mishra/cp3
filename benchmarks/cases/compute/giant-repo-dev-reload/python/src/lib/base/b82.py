def b82(x):
    acc = x + 82
    acc = (acc * 31 + 574) % 1000033
    acc = (acc * 131 + x * 17 + 82) % 1000199
    return acc
