const M=1000003; const MM=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&MM;x^=x>>7n;x^=(x<<17n)&MM;return x&MM};
function build(depth,seed){ const s=xs_next(seed); if(depth===0) return {kind:0,v:Number(s%1000n)}; const k=s%8n; if(k<1n) return {kind:3,l:build(depth-1,s)}; const a=build(depth-1,s), b=build(depth-1,xs_next(s)); if(k<5n) return {kind:1,l:a,r:b}; return {kind:2,l:a,r:b};}
const eval=(e,p)=> e.kind===0?(e.v+p)%M:e.kind===1?(eval(e.l,p)+eval(e.r,p))%M:e.kind===2?(eval(e.l,p)*eval(e.r,p))%M:(M-eval(e.l,p))%M;
const count=e=>1+(e.l?count(e.l):0)+(e.r?count(e.r):0);
const root=build(4,12345n); let total=0; for(let p=0;p<8;p++) total=(total+eval(root,p))%M; console.log(count(root)); console.log(total);