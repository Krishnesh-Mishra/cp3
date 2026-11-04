def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
from collections import deque
if __name__=="__main__":
    NV,NE=50000,50000*8
    adj=[[] for _ in range(NV)]; r=12345
    for _ in range(NE):
        r=xs_next(r); adj[r%NV].append((r>>24)%NV)
    total=0; seed=999
    for _ in range(4):
        seed=xs_next(seed); src=seed%NV; dist=[-1]*NV; dist[src]=0; q=deque([src])
        while q:
            u=q.popleft(); d=dist[u]; total+=d
            for w in adj[u]:
                if dist[w]<0: dist[w]=d+1; q.append(w)
    print(NV+NE); print(total)
