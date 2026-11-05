#include <cstdint>
#include <cstdio>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
int main(){
  const int N=5000; const int STEPS=50; const float dt=0.0625f; const float g=8.0f;
  std::vector<float> x(N),y(N),vx(N),vy(N);
  uint64_t r=12345;
  for(int i=0;i<N;i++){ r=xs_next(r); x[i]=(float)(r%1600)*0.0625f; r=xs_next(r); y[i]=(float)(r%1600)*0.0625f; r=xs_next(r); vx[i]=(float)((int)(r%200)-100)*0.0625f; r=xs_next(r); vy[i]=(float)((int)(r%200)-100)*0.0625f; }
  for(int s=0;s<STEPS;s++) for(int i=0;i<N;i++){ x[i]+=vx[i]*dt; y[i]+=vy[i]*dt; vy[i]-=g*dt; if(y[i]<0){ y[i]=-y[i]; vy[i]=-vy[i]*0.5f; } if(x[i]>=100.0f) x[i]-=100.0f; if(x[i]<0) x[i]+=100.0f; }
  long long total=0; for(int i=0;i<N;i++){ total+=(long long)(x[i]*100)+(long long)(y[i]*100)+(long long)(vx[i]*100)+(long long)(vy[i]*100); }
  total%=1000000007; if(total<0) total+=1000000007;
  printf("%d\n",N); printf("%lld\n",total);
}
