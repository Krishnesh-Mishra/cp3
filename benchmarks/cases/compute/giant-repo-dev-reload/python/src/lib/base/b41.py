def b41(x):
    acc = x + 41
    acc = (acc * 31 + 287) % 1000033
    acc = (acc * 131 + x * 17 + 41) % 1000199
    return acc
