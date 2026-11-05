MOD=1000003
def xs_next(x):
    x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
    x ^= x >> 7
    x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
    return x & 0xFFFFFFFFFFFFFFFF
def gen_expr(seed, corrupt):
    r=seed; ops=["+","-","*","/"]; s=""; r=xs_next(r); terms=3+(r%4)
    for i in range(terms):
        if i>0: r=xs_next(r); s+=ops[r%4]
        r=xs_next(r); n=r%99+1; s+=str(n)
    if corrupt and len(s)>4:
        pos=len(s)//2; s=s[:pos]+"x"+s[pos+1:]
    return s
class P:
    def __init__(self,s): self.s=s; self.i=0; self.err=False
    def expr(self):
        v=self.term()
        while not self.err and self.i<len(self.s) and (self.s[self.i]=="+" or self.s[self.i]=="-"):
            op=self.s[self.i]; self.i+=1; t=self.term(); v=v+t if op=="+" else v-t
        return v
    def term(self):
        v=self.factor()
        while not self.err and self.i<len(self.s) and (self.s[self.i]=="*" or self.s[self.i]=="/"):
            op=self.s[self.i]; self.i+=1; f=self.factor()
            if op=="*": v=v*f
            else:
                if f==0: self.err=True; break
                v=int(v/f) if (v<0) != (f<0) and v%f!=0 else v//f
        return v
    def factor(self):
        if self.i>=len(self.s): self.err=True; return 0
        c=self.s[self.i]
        if c=="(": 
            self.i+=1; v=self.expr()
            if self.i>=len(self.s) or self.s[self.i]!=")": self.err=True; return 0
            self.i+=1; return v
        if c=="-": self.i+=1; return -self.factor()
        if c>="0" and c<="9":
            v=0
            while self.i<len(self.s) and self.s[self.i]>="0" and self.s[self.i]<="9":
                v=v*10+int(self.s[self.i]); self.i+=1
            return v
        self.err=True; return 0
if __name__=="__main__":
    ok=errn=total=0
    for i in range(5000):
        s=gen_expr(12345+i, i%4==3); p=P(s); v=p.expr()
        if p.err or p.i!=len(s): errn+=1; continue
        ok+=1; total=(total+v)%MOD; 
        if total<0: total+=MOD
    print(ok); print(errn); print(total)
