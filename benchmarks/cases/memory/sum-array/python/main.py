import sys

def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF

def main():
    N, PASSES = 4_000_000, 8
    r = 12345
    data = []
    for _ in range(N):
        r = xs_next(r)
        data.append(r % 1000)
    total = 0
    for p in range(PASSES):
        total += sum(x ^ p for x in data)
    print(N); print(total)

if __name__ == "__main__":
    main()
