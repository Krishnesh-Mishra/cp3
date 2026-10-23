// typed-array-sum (js entry) -- fill a fixed Float64Array and Uint8Array,
// sum the floats for 32 rounds, then fold the float and byte totals into one
// integer checksum.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry. The float pattern is
// a multiple of 0.25 and every partial sum stays far below 2^53, so the
// addition is exact in IEEE-754 doubles.

const SIZE = 1000000;
const ROUNDS = 32;
const MOD = 1000000007;

const values = new Float64Array(SIZE);
const bytes = new Uint8Array(SIZE);
for (let i = 0; i < SIZE; i++) {
  values[i] = (i % 97) * 0.5 + (i % 13) * 0.25;
  bytes[i] = (i * 31 + 7) % 256;
}

let total = 0.0;
for (let round = 0; round < ROUNDS; round++) {
  let acc = 0.0;
  for (let i = 0; i < SIZE; i++) acc += values[i];
  total += acc;
}

let byteTotal = 0;
for (let i = 0; i < SIZE; i++) byteTotal += bytes[i];

const scaled = Math.trunc(total * 4.0);
let checksum = (scaled + byteTotal) % MOD;
checksum = (checksum * 31 + SIZE + ROUNDS) % MOD;
console.log(checksum);
