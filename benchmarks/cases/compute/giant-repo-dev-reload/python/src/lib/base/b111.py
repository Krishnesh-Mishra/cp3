def b111(x):
    acc = x + 111
    acc = (acc * 31 + 777) % 1000033
    acc = (acc * 131 + x * 17 + 111) % 1000199
    return acc
