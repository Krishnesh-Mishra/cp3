def b86(x):
    acc = x + 86
    acc = (acc * 31 + 602) % 1000033
    acc = (acc * 131 + x * 17 + 86) % 1000199
    return acc
