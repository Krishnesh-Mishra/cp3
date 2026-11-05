def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    log=[]; r=12345
    for i in range(100000):
        r=xs_next(r); k=r%8; id_=(r>>3)%100000
        if k<4: log.append((1,id_,((r>>20)%201)-100,((r>>40)%201)-100,0,0))
        elif k<6:
            wlen=4+(r%12); w=xs_next(r); w0=(w%26)+97; log.append((2,id_,wlen,0,w0,wlen))
        elif k<7: log.append((3,id_,(r>>20)%256,0,0,0))
        else: log.append((0,id_,0,0,0,0))
    total=0
    for p in range(4):
        for (kind,id_,a,b,w0,wlen) in log:
            if kind==0: total+=1
            elif kind==1: total+=id_+a+b
            elif kind==2: total+=id_+wlen+w0
            else: total+=id_+a
    print(len(log)); print(total)
