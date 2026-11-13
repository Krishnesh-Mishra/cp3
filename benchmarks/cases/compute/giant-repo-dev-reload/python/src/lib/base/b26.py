def b26(x):
    acc = x + 26
    acc = (acc * 31 + 182) % 1000033
    acc = (acc * 131 + x * 17 + 26) % 1000199
    return acc
