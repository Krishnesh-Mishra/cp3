def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    r=12345; items=[]; marks=[]
    for i in range(1000000):
        r=xs_next(r); items.append((r%1000000, i%97))
        if i%64==0: marks.append(len(items)-1)
    print(len(items)); print(len(marks)); print(sum(k for k,_ in items)); print(sum(items[m][1] for m in marks))
