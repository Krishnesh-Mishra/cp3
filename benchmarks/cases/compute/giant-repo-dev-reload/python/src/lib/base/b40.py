def b40(x):
    acc = x + 40
    acc = (acc * 31 + 280) % 1000033
    acc = (acc * 131 + x * 17 + 40) % 1000199
    return acc
