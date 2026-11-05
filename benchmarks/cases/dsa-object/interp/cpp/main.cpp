#include <cstdint>
#include <cstdio>
#include <memory>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
static const long long M=1000003;
struct Expr{ virtual ~Expr()=default; virtual long long eval(long long p) const=0; };
struct Num: Expr{ int32_t v; long long eval(long long p) const override { return (v+p)%M; } };
struct Add: Expr{ std::unique_ptr<Expr> l,r; long long eval(long long p) const override { return (l->eval(p)+r->eval(p))%M; } };
struct Mul: Expr{ std::unique_ptr<Expr> l,r; long long eval(long long p) const override { return (l->eval(p)*r->eval(p))%M; } };
struct Neg: Expr{ std::unique_ptr<Expr> of; long long eval(long long p) const override { return (M - of->eval(p))%M; } };
using Ref=std::unique_ptr<Expr>; static long long cnt=0;
static Ref build(int depth, uint64_t seed){ uint64_t s=xs_next(seed); cnt++; if(depth==0){ auto n=std::make_unique<Num>(); n->v=(int32_t)(s%1000); return n; } uint64_t k=s%8; if(k<1){ auto n=std::make_unique<Neg>(); n->of=build(depth-1,s); return n; } auto a=build(depth-1,s); auto b=build(depth-1,xs_next(s)); if(k<5){ auto n=std::make_unique<Add>(); n->l=std::move(a); n->r=std::move(b); return n; } auto n=std::make_unique<Mul>(); n->l=std::move(a); n->r=std::move(b); return n; }
int main(){ Ref r=build(4,12345); long long total=0; for(long long p=0;p<8;p++) total=(total+r->eval(p))%M; printf("%lld\n",cnt); printf("%lld\n",total); }
