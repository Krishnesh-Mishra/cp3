#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static inline uint64_t xs_next(uint64_t x){ x^=x<<13; x^=x>>7; x^=x<<17; return x; }
static const long long MOD=1000003;
static std::string gen_expr(uint64_t seed, bool corrupt){
  uint64_t r=seed; std::string s;
  char ops[4]={'+','-','*','/'};
  auto num=[&](){ r=xs_next(r); return (int)(r%99)+1; };
  r=xs_next(r); int terms=3+(int)(r%4);
  for(int i=0;i<terms;i++){ if(i){ r=xs_next(r); s.push_back(ops[r%4]); } int n=num(); if(n>=10){ s.push_back((char)('0'+n/10)); s.push_back((char)('0'+n%10)); } else s.push_back((char)('0'+n)); }
  if(corrupt && s.size()>4) s[s.size()/2]='x';
  return s;
}
struct P{ const std::string& s; size_t i; bool err; P(const std::string& str):s(str),i(0),err(false){}
  long long parse_expr(){ long long v=parse_term(); while(!err && i<s.size() && (s[i]=='+'||s[i]=='-')){ char op=s[i++]; long long t=parse_term(); v=(op=='+')?v+t:v-t; } return v; }
  long long parse_term(){ long long v=parse_factor(); while(!err && i<s.size() && (s[i]=='*'||s[i]=='/')){ char op=s[i++]; long long f=parse_factor(); if(op=='*') v=v*f; else { if(f==0){err=true; break;} v=v/f; } } return v; }
  long long parse_factor(){ if(i>=s.size()){err=true; return 0;} if(s[i]=='('){ i++; long long v=parse_expr(); if(i>=s.size()||s[i]!=')'){err=true; return 0;} i++; return v; } if(s[i]=='-'){ i++; return -parse_factor(); } if(s[i]>='0'&&s[i]<='9'){ long long v=0; while(i<s.size()&&s[i]>='0'&&s[i]<='9'){ v=v*10+(s[i]-'0'); i++; } return v; } err=true; return 0; }
};
int main(){
  long long ok=0, errn=0, total=0;
  for(long long i=0;i<5000;i++){
    bool corrupt=(i%4==3);
    std::string s=gen_expr(12345+i, corrupt);
    P p(s); long long v=p.parse_expr();
    if(p.err || p.i!=s.size()){ errn++; continue; }
    ok++; total=(total+v)%MOD; if(total<0) total+=MOD;
  }
  printf("%lld\n",ok); printf("%lld\n",errn); printf("%lld\n",total);
}
