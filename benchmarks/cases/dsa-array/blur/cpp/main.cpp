#include <cstdint>
#include <cstdio>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
int main(){ const long long W=256; const int PASSES=16; std::vector<unsigned char> a,b; uint64_t r=12345; for(long long i=0;i<W*W;i++){ r=xs_next(r); a.push_back((unsigned char)(r%256)); b.push_back(0);} auto blur=[&](const std::vector<unsigned char>& s, std::vector<unsigned char>& d){ for(long long y=1;y<W-1;y++) for(long long x=1;x<W-1;x++){ long long i=y*W+x; int sm=s[i-W-1]+s[i-W]*2+s[i-W+1]+s[i-1]*2+s[i]*4+s[i+1]*2+s[i+W-1]+s[i+W]*2+s[i+W+1]; d[i]=(unsigned char)(sm>>4);} }; for(int p=0;p<PASSES/2;p++){ blur(a,b); blur(b,a);} long long total=0; for(auto v:a) total+=v; printf("%lld\n",W); printf("%lld\n",total); }
