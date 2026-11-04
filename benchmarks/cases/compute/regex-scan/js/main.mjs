// regex-scan (js entry) -- scan a 240,000-byte generated text with /[0-9]+/g
// for 8 rounds, count the matches, sum their numeric values, and fold each
// round into one checksum.
//
// Run: node main.mjs
//
// Expected output: 735703090

const UNIT = "alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n";
const REPEATS = 5000;
const ROUNDS = 8;
const MOD = 1000000007;
const PATTERN = /[0-9]+/g;

const TEXT = UNIT.repeat(REPEATS);

let checksum = 0;
for (let round = 0; round < ROUNDS; round++) {
  const matches = TEXT.match(PATTERN);
  const count = matches.length;
  let sum = 0;
  for (let i = 0; i < count; i++) {
    sum += Number(matches[i]);
  }
  checksum = (checksum * 31 + count + sum) % MOD;
}

console.log(checksum);
