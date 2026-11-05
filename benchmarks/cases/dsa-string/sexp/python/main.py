def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    text=""; seed=12345
    def gen(depth, seed):
        global text
        r=xs_next(seed)
        if depth==0 or r%4==0:
            if (r>>8)%2==0:
                n=3+(r%12)
                for _ in range(n):
                    r=xs_next(r); text+=chr(97+(r%26))
            else:
                text+=str((r>>16)%100000)
            text+=" "
            return r
        text+="("
        kids=2+((r>>24)%4)
        for _ in range(kids): r=gen(depth-1,r)
        text+=") "
        return r
    for _ in range(100): seed=gen(4,seed)
    h=1469598103934665603
    for ch in text: h=((h^ord(ch))*1099511628211) & 0xFFFFFFFFFFFFFFFF
    print(len(text)); print(h)
