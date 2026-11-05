import sys
sys.setrecursionlimit(100000)
def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
count=0
class Node:
    __slots__=("id","tx","ty","tz","rot","kids")
    def __init__(self,id_,tx,ty,tz,rot): self.id=id_; self.tx=tx; self.ty=ty; self.tz=tz; self.rot=rot; self.kids=[]
def build(depth,seed):
    global count
    r=xs_next(seed); n=Node(count,r%9,(r>>16)%9,(r>>24)%9,(r>>12)%4); count+=1
    if depth==0: return n
    k=1+((r>>40)%4)
    for i in range(k): n.kids.append(build(depth-1,xs_next(r+i)))
    return n
def animate(n,p):
    if n.id%7==p: n.rot=(n.rot+1)%4; n.tx=(n.tx+1)%9
    for c in n.kids: animate(c,p)
def gather(n):
    t=n.tx+n.ty+n.tz
    for c in n.kids: t+=gather(c)
    return t
if __name__=="__main__":
    root=build(4,12345); total=0
    for f in range(16): animate(root,f%7); total=(total+gather(root))%1000000007
    print(count); print(total)
