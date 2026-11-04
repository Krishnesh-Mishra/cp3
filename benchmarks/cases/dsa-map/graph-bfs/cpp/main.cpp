#include <cstdint>
#include <cstdio>
#include <vector>
#include <queue>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
int main(){ const long long NV=50000, NE=NV*8; std::vector<std::vector<int32_t>> adj((size_t)NV); uint64_t r=12345; for(long long e=0;e<NE;e++){ r=xs_next(r); adj[(size_t)(r%NV)].push_back((int32_t)((r>>24)%NV)); } std::vector<int32_t> dist((size_t)NV); std::queue<int32_t> q; long long total=0; uint64_t seed=999; for(int s=0;s<4;s++){ seed=xs_next(seed); long long src=(long long)(seed%NV); for(long long v=0;v<NV;v++) dist[(size_t)v]=-1; while(!q.empty()) q.pop(); dist[(size_t)src]=0; q.push((int32_t)src); while(!q.empty()){ int32_t u=q.front(); q.pop(); long long d=dist[(size_t)u]; total+=d; for(int32_t w: adj[(size_t)u]) if(dist[(size_t)w]<0){ dist[(size_t)w]=(int32_t)(d+1); q.push(w);} } } printf("%lld\n", (long long)(NV+NE)); printf("%lld\n", total); }
