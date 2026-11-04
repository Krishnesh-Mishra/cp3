let r=12345n; const M=0xFFFFFFFFFFFFFFFFn; const xs_next=x=>{x^=(x<<13n)&M;x^=x>>7n;x^=(x<<17n)&M;return x&M};
const words=[]; for(let i=0;i<100000;i++){r=xs_next(r); const len=Number(3n+(r%18n)); let w=""; for(let j=0;j<len;j++){r=xs_next(r); w+=String.fromCharCode(97+Number(r%26n));} words.push(w);}
let total=0; for(let p=0;p<4;p++) for(const w of words) total+=w.length+w.charCodeAt(0)+w.charCodeAt(w.length-1);
console.log(words.length); console.log(total);