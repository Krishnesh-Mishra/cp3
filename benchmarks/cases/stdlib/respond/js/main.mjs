const MM=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&MM;x^=x>>7n;x^=(x<<17n)&MM;return x&MM};
let out=""; let r=12345n;
for(let i=0;i<10000;i++){ r=xs_next(r); const id=Number(r%100000n); r=xs_next(r); const len=4+Number(r%9n); let name=""; for(let j=0;j<len;j++){ r=xs_next(r); if(j%8===7) name+=(r%2n===0n?String.fromCharCode(34):String.fromCharCode(92)); else name+=String.fromCharCode(97+Number(r%26n)); } r=xs_next(r); const score=Number(r%1000n); const active=((r>>8n)%2n===0n); out+='{"id":'+id+',"name":"'; for(const c of name){ if(c===String.fromCharCode(34)||c===String.fromCharCode(92)) out+=String.fromCharCode(92); out+=c; } out+='","score":'+score+',"active":'+(active?"true":"false")+'},'; }
let h=1469598103934665603n; for(let i=0;i<out.length;i++){ h=(h^BigInt(out.charCodeAt(i)))*1099511628211n; h&=MM; }
console.log(out.length); console.log(h.toString());
