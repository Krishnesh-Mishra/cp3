// math-kernel (js entry) -- evaluate sqrt, floor, abs, pow and clamp over
// 5,000,000 points, quantize each result to an integer and fold one checksum.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry. sqrt is correctly
// rounded, pow uses an integer exponent, and every term is quantized to an
// integer before folding, so the checksum is exact across libms.

const COUNT = 5000000;
const MOD = 1000000007;

let checksum = 0;
let sqrtTotal = 0.0;
for (let i = 1; i <= COUNT; i++) {
  const x = i * 0.000001;
  const s = Math.sqrt(i);
  sqrtTotal += s;
  const f = Math.floor(x + 0.5);
  const a = Math.abs(x - 2.5);
  const p = Math.trunc(Math.pow(i, 2));
  const c = Math.min(Math.max(x * 1000.0, 0.0), 1000.0);
  const mixed = s + f + a + c;
  const quantum = Math.trunc(mixed * 1000000.0);
  checksum = (checksum * 31 + quantum + p) % MOD;
}
const sqrtScaled = Math.trunc(sqrtTotal * 1000.0);
checksum = (checksum * 31 + sqrtScaled) % MOD;
console.log(checksum);
