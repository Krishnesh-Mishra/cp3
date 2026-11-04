// Goose is not a JS runtime; keep the workload in BigInt for u64 precision.
const N = 4000000, PASSES = 8;
let r = 12345n;
const xs_next = (x) => { x ^= (x << 13n) & 0xFFFFFFFFFFFFFFFFn; x ^= x >> 7n; x ^= (x << 17n) & 0xFFFFFFFFFFFFFFFFn; return x & 0xFFFFFFFFFFFFFFFFn; };
const data = new Int32Array(N);
for (let i = 0; i < N; i++) { r = xs_next(r); data[i] = Number(r % 1000n); }
let total = 0;
for (let p = 0; p < PASSES; p++) for (let j = 0; j < N; j++) total += data[j] ^ p;
console.log(N); console.log(total);
