def b126(x):
    acc = x + 126
    acc = (acc * 31 + 882) % 1000033
    acc = (acc * 131 + x * 17 + 126) % 1000199
    return acc
