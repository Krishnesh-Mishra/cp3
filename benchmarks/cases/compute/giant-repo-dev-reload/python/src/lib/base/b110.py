def b110(x):
    acc = x + 110
    acc = (acc * 31 + 770) % 1000033
    acc = (acc * 131 + x * 17 + 110) % 1000199
    return acc
