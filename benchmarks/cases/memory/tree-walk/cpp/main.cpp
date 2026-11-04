#include <cstdint>
#include <cstdio>
#include <memory>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
static inline uint64_t xs_mod(uint64_t x,uint64_t k){ return x%k; }
struct TNode{ int32_t v; std::unique_ptr<TNode> l,r; };
using Owned = std::unique_ptr<TNode>;
static Owned build(int depth, uint64_t seed){
  uint64_t s=xs_next(seed);
  auto n=std::make_unique<TNode>(); n->v=(int32_t)xs_mod(s,1000);
  if(depth){ n->l=build(depth-1,s); n->r=build(depth-1,xs_next(s+1)); }
  return n;
}
static long long sum_tree(const TNode* n, long long p){
  if(!n) return 0;
  long long s = n->v ^ p;
  if(n->l) s += sum_tree(n->l.get(), p);
  if(n->r) s += sum_tree(n->r.get(), p);
  return s;
}
int main(){
  const int DEPTH=13;
  Owned root = build(DEPTH, 12345);
  long long count = (1LL << (DEPTH+1)) - 1;
  long long total=0;
  for(long long p=0;p<8;p++) total += sum_tree(root.get(), p);
  printf("%lld\n", count); printf("%lld\n", total);
  return 0;
}
