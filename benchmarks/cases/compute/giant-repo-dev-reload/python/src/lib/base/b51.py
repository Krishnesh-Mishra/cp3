def b51(x):
    acc = x + 51
    acc = (acc * 31 + 357) % 1000033
    acc = (acc * 131 + x * 17 + 51) % 1000199
    return acc
