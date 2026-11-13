def b20(x):
    acc = x + 20
    acc = (acc * 31 + 140) % 1000033
    acc = (acc * 131 + x * 17 + 20) % 1000199
    return acc
