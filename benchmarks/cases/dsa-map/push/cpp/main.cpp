#include <cstdint>
#include <cstdio>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
struct Item{int32_t key,val;};
int main(){ std::vector<Item> items; std::vector<size_t> marks; uint64_t r=12345; for(long long i=0;i<1000000;i++){ r=xs_next(r); items.push_back({(int32_t)(r%1000000),(int32_t)(i%97)}); if(i%64==0) marks.push_back(items.size()-1);} long long total=0; for(auto &it:items) total+=it.key; long long mt=0; for(size_t m:marks) mt+=items[m].val; printf("%lld\n", (long long)items.size()); printf("%lld\n", (long long)marks.size()); printf("%lld\n", total); printf("%lld\n", mt); }