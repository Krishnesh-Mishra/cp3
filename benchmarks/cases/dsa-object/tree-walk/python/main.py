import sys
sys.setrecursionlimit(100000)

def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF

class TNode:
    __slots__ = ("v","l","r")
    def __init__(self,v,l=None,r=None): self.v=v; self.l=l; self.r=r

def build(depth, seed):
    s = xs_next(seed)
    if depth == 0: return TNode(s % 1000)
    return TNode(s % 1000, build(depth-1, s), build(depth-1, xs_next(s+1)))

def sum_tree(n,p):
    if n is None: return 0
    s = n.v ^ p
    s += sum_tree(n.l,p); s += sum_tree(n.r,p)
    return s

if __name__ == "__main__":
    root = build(13, 12345)
    total = sum(sum_tree(root,p) for p in range(8))
    print(16383); print(total)
