def b127(x):
    acc = x + 127
    acc = (acc * 31 + 889) % 1000033
    acc = (acc * 131 + x * 17 + 127) % 1000199
    return acc
