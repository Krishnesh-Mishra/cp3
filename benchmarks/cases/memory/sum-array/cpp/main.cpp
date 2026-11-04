#include <cstdint>
#include <cstdio>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
static inline uint64_t xs_mod(uint64_t x,uint64_t k){ return x%k; }
int main(){
  const long long N=4000000; const int PASSES=8;
  std::vector<int32_t> data; data.reserve(N);
  uint64_t r=12345;
  for(long long i=0;i<N;i++){ r=xs_next(r); data.push_back((int32_t)xs_mod(r,1000)); }
  long long total=0;
  for(int p=0;p<PASSES;p++) for(int32_t x: data) total+=x^p;
  printf("%lld\n", N); printf("%lld\n", total);
  return 0;
}
