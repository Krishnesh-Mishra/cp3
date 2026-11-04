def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__ == "__main__":
    r = 12345
    words = []
    for _ in range(100000):
        r = xs_next(r)
        ln = 3 + (r % 18)
        w = ""
        for _ in range(ln):
            r = xs_next(r)
            w += chr(97 + (r % 26))
        words.append(w)
    total = sum(len(w) + ord(w[0]) + ord(w[-1]) for _ in range(4) for w in words)
    print(len(words))
    print(total)
