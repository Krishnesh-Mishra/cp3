def b108(x):
    acc = x + 108
    acc = (acc * 31 + 756) % 1000033
    acc = (acc * 131 + x * 17 + 108) % 1000199
    return acc
