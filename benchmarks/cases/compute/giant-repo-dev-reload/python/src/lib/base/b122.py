def b122(x):
    acc = x + 122
    acc = (acc * 31 + 854) % 1000033
    acc = (acc * 131 + x * 17 + 122) % 1000199
    return acc
