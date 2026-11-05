const MM=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&MM;x^=x>>7n;x^=(x<<17n)&MM;return x&MM};
let log=[]; let r=12345n;
for(let i=0;i<100000;i++){ r=xs_next(r); const k=r%8n; const id=Number((r>>3n)%100000n); let e={kind:0,id,a:0,b:0,w0:0,wlen:0}; if(k<4n){ e.kind=1; e.a=Number((r>>20n)%201n)-100; e.b=Number((r>>40n)%201n)-100; } else if(k<6n){ e.kind=2; e.wlen=4+Number(r%12n); const w=xs_next(r); e.w0=Number(w%26n)+97; } else if(k<7n){ e.kind=3; e.a=Number((r>>20n)%256n); } else e.kind=0; log.push(e); }
let total=0; for(let p=0;p<4;p++) for(const e of log){ if(e.kind===0) total+=1; else if(e.kind===1) total+=e.id+e.a+e.b; else if(e.kind===2) total+=e.id+e.wlen+e.w0; else total+=e.id+e.a; }
console.log(log.length); console.log(total);