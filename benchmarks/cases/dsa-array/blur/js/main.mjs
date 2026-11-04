const W=256; const M=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&M;x^=x>>7n;x^=(x<<17n)&M;return x&M};
let r=12345n; const a=new Uint8Array(W*W),b=new Uint8Array(W*W); for(let i=0;i<W*W;i++){r=xs_next(r); a[i]=Number(r%256n);}
const blur=(s,d)=>{for(let y=1;y<W-1;y++)for(let x=1;x<W-1;x++){const i=y*W+x; d[i]=(s[i-W-1]+s[i-W]*2+s[i-W+1]+s[i-1]*2+s[i]*4+s[i+1]*2+s[i+W-1]+s[i+W]*2+s[i+W+1])>>4;}};
for(let p=0;p<8;p++){blur(a,b);blur(b,a);}
let total=0; for(const v of a) total+=v; console.log(W); console.log(total);