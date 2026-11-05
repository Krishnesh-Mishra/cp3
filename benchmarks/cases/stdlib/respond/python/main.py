def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    out=""; r=12345
    for i in range(10000):
        r=xs_next(r); id_=r%100000
        r=xs_next(r); length=4+(r%9)
        name=""
        for j in range(length):
            r=xs_next(r)
            if j%8==7: name += chr(34) if (r%2==0) else chr(92)
            else: name += chr(97+(r%26))
        r=xs_next(r); score=r%1000; active=((r>>8)%2==0)
        out += '{"id":%d,"name":"' % id_
        for c in name:
            if c==chr(34) or c==chr(92): out += chr(92)
            out += c
        out += '","score":%d,"active":%s},' % (score, "true" if active else "false")
    h=1469598103934665603
    for ch in out: h=((h^ord(ch))*1099511628211) & 0xFFFFFFFFFFFFFFFF
    print(len(out)); print(h)
