const MM=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&MM;x^=x>>7n;x^=(x<<17n)&MM;return x&MM};
const N=5000,STEPS=50; const dt=Math.fround(0.0625),g=Math.fround(8);
const x=new Float32Array(N),y=new Float32Array(N),vx=new Float32Array(N),vy=new Float32Array(N);
let r=12345n; for(let i=0;i<N;i++){ r=xs_next(r); x[i]=Number(r%1600n)*0.0625; r=xs_next(r); y[i]=Number(r%1600n)*0.0625; r=xs_next(r); vx[i]=(Number(r%200n)-100)*0.0625; r=xs_next(r); vy[i]=(Number(r%200n)-100)*0.0625; }
for(let s=0;s<STEPS;s++) for(let i=0;i<N;i++){ x[i]=Math.fround(x[i]+Math.fround(vx[i]*dt)); y[i]=Math.fround(y[i]+Math.fround(vy[i]*dt)); vy[i]=Math.fround(vy[i]-Math.fround(g*dt)); if(y[i]<0){ y[i]=-y[i]; vy[i]=Math.fround(-vy[i]*Math.fround(0.5)); } if(x[i]>=100){ x[i]=Math.fround(x[i]-100); } if(x[i]<0){ x[i]=Math.fround(x[i]+100); } }
let total=0; for(let i=0;i<N;i++) total+=Math.trunc(x[i]*100)+Math.trunc(y[i]*100)+Math.trunc(vx[i]*100)+Math.trunc(vy[i]*100);
total%=1000000007; if(total<0) total+=1000000007; console.log(N); console.log(total);