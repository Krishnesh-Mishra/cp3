const MM=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&MM;x^=x>>7n;x^=(x<<17n)&MM;return x&MM};
let count=0;
function build(depth,seed){ const r=xs_next(seed); const n={id:count++,tx:Number(r%9n),ty:Number((r>>16n)%9n),tz:Number((r>>24n)%9n),rot:Number((r>>12n)%4n),kids:[]}; if(depth===0) return n; const k=1+Number((r>>40n)%4n); for(let i=0n;i<BigInt(k);i++) n.kids.push(build(depth-1,xs_next(r+i))); return n; }
function animate(n,p){ if(n.id%7===p){ n.rot=(n.rot+1)%4; n.tx=(n.tx+1)%9; } for(const c of n.kids) animate(c,p); }
function gather(n){ let t=n.tx+n.ty+n.tz; for(const c of n.kids) t+=gather(c); return t; }
const root=build(4,12345n); let total=0; for(let f=0;f<16;f++){ animate(root,f%7); total=(total+gather(root))%1000000007; } console.log(count); console.log(total);