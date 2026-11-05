#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
struct E{int kind; int id; int a,b; std::string w;};
int main(){
  std::vector<E> log; log.reserve(100000);
  uint64_t r=12345;
  for(long long i=0;i<100000;i++){
    r=xs_next(r); uint64_t k=r%8; int id=(int)((r>>3)%100000);
    E e{}; e.id=id;
    if(k<4){ e.kind=1; e.a=(int)((r>>20)%201)-100; e.b=(int)((r>>40)%201)-100; }
    else if(k<6){ e.kind=2; e.a=4+(int)(r%12); e.b=(int)(r%26); uint64_t w=r; for(int j=0;j<e.a;j++){ w=xs_next(w); e.w.push_back((char)('a'+w%26)); } }
    else if(k<7){ e.kind=3; e.a=(int)((r>>20)%256); }
    else { e.kind=0; }
    log.push_back(e);
  }
  long long total=0;
  for(int p=0;p<4;p++) for(auto &e: log){
    if(e.kind==0) total+=1;
    else if(e.kind==1) total+=(long long)e.id+e.a+e.b;
    else if(e.kind==2) total+=(long long)e.id+e.a+(unsigned char)e.w[0];
    else total+=(long long)e.id+e.a;
  }
  printf("%lld\n",(long long)log.size()); printf("%lld\n",total);
}
