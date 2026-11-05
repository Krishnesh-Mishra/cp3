import struct
def f32(v): return struct.unpack("f",struct.pack("f",v))[0]
def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
if __name__=="__main__":
    N,STEPS=5000,50; dt=f32(0.0625); g=f32(8.0)
    x=[0.0]*N; y=[0.0]*N; vx=[0.0]*N; vy=[0.0]*N
    r=12345
    for i in range(N):
        r=xs_next(r); x[i]=f32((r%1600)*0.0625)
        r=xs_next(r); y[i]=f32((r%1600)*0.0625)
        r=xs_next(r); vx[i]=f32(((r%200)-100)*0.0625)
        r=xs_next(r); vy[i]=f32(((r%200)-100)*0.0625)
    for s in range(STEPS):
        for i in range(N):
            x[i]=f32(x[i]+f32(vx[i]*dt))
            y[i]=f32(y[i]+f32(vy[i]*dt))
            vy[i]=f32(vy[i]-f32(g*dt))
            if y[i]<0: y[i]=-y[i]; vy[i]=f32(-vy[i]*f32(0.5))
            if x[i]>=100: x[i]=f32(x[i]-100)
            if x[i]<0: x[i]=f32(x[i]+100)
    total=0
    for i in range(N): total+=int(x[i]*100)+int(y[i]*100)+int(vx[i]*100)+int(vy[i]*100)
    total%=1000000007
    if total<0: total+=1000000007
    print(N); print(total)
