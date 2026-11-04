#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
int main(){ uint64_t r=12345; std::vector<std::string> words; for(long long i=0;i<100000;i++){ r=xs_next(r); long long len=3+(long long)(r%18); std::string w; for(long long j=0;j<len;j++){ r=xs_next(r); w.push_back((char)('a'+r%26)); } words.push_back(w);} long long total=0; for(int p=0;p<4;p++) for(auto &w: words) total+=(long long)w.size()+(unsigned char)w[0]+(unsigned char)w[w.size()-1]; printf("%lld\n",(long long)words.size()); printf("%lld\n",total); }
