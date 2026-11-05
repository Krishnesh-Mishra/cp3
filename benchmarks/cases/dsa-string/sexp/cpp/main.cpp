#include <cstdint>
#include <cstdio>
#include <string>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
static std::string text;
static void emit_num(long long v){ if(v==0){text.push_back('0');return;} char t[24];int k=0; while(v>0){t[k++]='0'+v%10;v/=10;} while(k>0) text.push_back(t[--k]); }
static uint64_t gen(int depth, uint64_t seed){ uint64_t r=xs_next(seed); if(depth==0||(r%4)==0){ if(((r>>8)&1)==0){ long long n=3+(long long)(r%12); for(long long i=0;i<n;i++){ r=xs_next(r); text.push_back((char)('a'+r%26)); } } else emit_num((long long)(r>>16)%100000); text.push_back(' '); return r; } text.push_back('('); long long kids=2+((r>>24)%4); for(long long k=0;k<kids;k++) r=gen(depth-1,r); text.push_back(')'); text.push_back(' '); return r; }
int main(){ uint64_t seed=12345; for(int i=0;i<100;i++) seed=gen(4,seed); uint64_t h=1469598103934665603ULL; for(char c: text){ h=(h^(unsigned char)c)*1099511628211ULL; } printf("%lld\n",(long long)text.size()); printf("%llu\n",(unsigned long long)h); }
