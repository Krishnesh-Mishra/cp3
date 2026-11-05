#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
int main(){
  std::string out;
  uint64_t r=12345;
  for(int i=0;i<10000;i++){
    r=xs_next(r); int id=(int)(r%100000);
    r=xs_next(r); int len=4+(int)(r%9);
    std::string name;
    for(int j=0;j<len;j++){ r=xs_next(r); if(j%8==7){ name.push_back((r%2)?'\\':'"'); } else name.push_back((char)('a'+r%26)); }
    r=xs_next(r); int score=(int)(r%1000); bool active=(r>>8)%2==0;
    out += "{\"id\":";
    char buf[16]; int k=0; if(id==0){buf[k++]='0';} else { char t[16]; int m=0; while(id>0){t[m++]='0'+id%10; id/=10;} while(m>0) buf[k++]=t[--m]; } buf[k]=0; out += buf;
    out += ",\"name\":\"";
    for(char c: name){ if(c=='"' || c=='\\') out.push_back('\\'); out.push_back(c); }
    out += "\",\"score\":";
    k=0; if(score==0){buf[k++]='0';} else { char t[16]; int m=0; while(score>0){t[m++]='0'+score%10; score/=10;} while(m>0) buf[k++]=t[--m]; } buf[k]=0; out += buf;
    out += ",\"active\":"; out += active ? "true" : "false"; out += "},";
  }
  uint64_t h=1469598103934665603ULL; for(unsigned char c: out) h=(h^c)*1099511628211ULL;
  printf("%lld\n",(long long)out.size()); printf("%llu\n",(unsigned long long)h);
}
