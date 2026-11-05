#include <cstdint>
#include <cstdio>
#include <vector>
#include <memory>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
struct Node{ int id; int tx,ty,tz,rot; std::vector<std::unique_ptr<Node>> kids; };
static int gcount=0;
static std::unique_ptr<Node> build(int depth, uint64_t seed){
  uint64_t r=xs_next(seed); auto n=std::make_unique<Node>(); n->id=gcount++; n->tx=(int)(r%9); n->ty=(int)((r>>16)%9); n->tz=(int)((r>>24)%9); n->rot=(int)((r>>12)%4);
  if(depth==0) return n;
  long long k=1+((r>>40)%4); for(long long i=0;i<k;i++) n->kids.push_back(build(depth-1, xs_next(r+i)));
  return n;
}
static void animate(Node* n, long long phase){ if(n->id%7==phase){ n->rot=(n->rot+1)%4; n->tx=(n->tx+1)%9; } for(auto &c: n->kids) animate(c.get(),phase); }
static long long gather(const Node* n){ long long t=n->tx+n->ty+n->tz; for(auto &c: n->kids) t+=gather(c.get()); return t; }
int main(){
  auto root=build(4,12345); long long total=0;
  for(long long f=0;f<16;f++){ animate(root.get(),f%7); total=(total+gather(root.get()))%1000000007; }
  printf("%d\n",gcount); printf("%lld\n",total);
}
