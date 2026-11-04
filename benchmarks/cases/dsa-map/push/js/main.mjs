let r=12345n; const M=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&M; x^=x>>7n; x^=(x<<17n)&M; return x&M};
const items=[],marks=[];
for(let i=0;i<1000000;i++){r=xs_next(r); items.push([r%1000000n,BigInt(i%97)]); if(i%64===0) marks.push(items.length-1);}
let total=0n,mt=0n; for(const it of items) total+=it[0]; for(const m of marks) mt+=items[m][1];
console.log(items.length); console.log(marks.length); console.log(total.toString()); console.log(mt.toString());