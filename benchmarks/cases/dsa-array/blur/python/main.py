def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    W=256; r=12345; a=[0]*(W*W); b=[0]*(W*W)
    for i in range(W*W): r=xs_next(r); a[i]=r%256
    def blur(s,d):
        for y in range(1,W-1):
            for x in range(1,W-1):
                i=y*W+x; sm=s[i-W-1]+s[i-W]*2+s[i-W+1]+s[i-1]*2+s[i]*4+s[i+1]*2+s[i+W-1]+s[i+W]*2+s[i+W+1]; d[i]=sm>>4
    for _ in range(8): blur(a,b); blur(b,a)
    print(W); print(sum(a))
