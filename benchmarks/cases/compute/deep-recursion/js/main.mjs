// deep-recursion (js entry) -- 200 fresh recursive sumTo(5000) calls, each
// recursing 5,000 frames and returning the triangular sum, folded into one
// checksum.
//
// Run: node main.mjs
//
// Expected output: 292249866

const DEPTH = 5000;
const ROUNDS = 200;
const MOD = 1000000007;

function sumTo(n) {
  if (n === 0) {
    return 0;
  }
  return n + sumTo(n - 1);
}

let checksum = 0;
for (let round = 0; round < ROUNDS; round++) {
  checksum = (checksum * 31 + sumTo(DEPTH)) % MOD;
}

console.log(checksum);
