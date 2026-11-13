def b0(x):
    acc = x + 0
    acc = (acc * 31 + 0) % 1000033
    acc = (acc * 131 + x * 17 + 0) % 1000199
    return acc
greeting = "Hello, world!"
