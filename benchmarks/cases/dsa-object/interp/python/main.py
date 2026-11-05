def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
M=1000003
class E:
    __slots__=("kind","v","l","r")
    def __init__(self,kind,v=0,l=None,r=None): self.kind=kind; self.v=v; self.l=l; self.r=r
def build(depth, seed):
    s=xs_next(seed)
    if depth==0: return E(0, s%1000)
    k=s%8
    if k<1: return E(3, l=build(depth-1,s))
    a=build(depth-1,s); b=build(depth-1,xs_next(s))
    return E(1 if k<5 else 2, l=a, r=b)
def eval_(e,p):
    if e.kind==0: return (e.v+p)%M
    if e.kind==1: return (eval_(e.l,p)+eval_(e.r,p))%M
    if e.kind==2: return (eval_(e.l,p)*eval_(e.r,p))%M
    return (M-eval_(e.l,p))%M
def count(e): return 1+(count(e.l) if e.l else 0)+(count(e.r) if e.r else 0)
if __name__=="__main__":
    root=build(4,12345); total=sum(eval_(root,p) for p in range(8))%M
    print(count(root)); print(total)
