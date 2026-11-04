class TNode { constructor(v,l=null,r=null){this.v=v;this.l=l;this.r=r;} }
const xs_next = (x) => { x ^= (x << 13n) & 0xFFFFFFFFFFFFFFFFn; x ^= x >> 7n; x ^= (x << 17n) & 0xFFFFFFFFFFFFFFFFn; return x & 0xFFFFFFFFFFFFFFFFn; };
function build(depth, seed) {
  const s = xs_next(seed);
  if (depth === 0) return new TNode(s % 1000n);
  return new TNode(s % 1000n, build(depth-1, s), build(depth-1, xs_next(s+1n)));
}
function sumTree(n,p){ if(n===null) return 0n; let s=n.v^p; s+=sumTree(n.l,p); s+=sumTree(n.r,p); return s; }
const root = build(13, 12345n);
let total = 0n;
for (let p = 0n; p < 8n; p++) total += sumTree(root, p);
console.log(16383); console.log(total.toString());
