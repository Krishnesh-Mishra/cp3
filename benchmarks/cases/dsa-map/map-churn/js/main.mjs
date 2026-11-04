// map-churn (js entry) -- 100,000 integer keys in a Map, two rounds of
// delete every 10th key, reinsert, update every 7th key, then fold all keys
// in ascending order.
//
// Run: node main.mjs
//
// Expected output: 942471769

const KEYS = 100000;
const ROUNDS = 2;
const MOD = 1000000007;

const map = new Map();
for (let i = 0; i < KEYS; i++) {
  map.set(i, i * 3 + 1);
}

let checksum = 0;
for (let r = 0; r < ROUNDS; r++) {
  for (let i = 0; i < KEYS; i += 10) {
    map.delete(i);
  }

  for (let i = 0; i < KEYS; i += 10) {
    map.set(i, i * 5 + r + 2);
  }

  for (let i = 0; i < KEYS; i += 7) {
    map.set(i, (map.get(i) ?? 0) + 1);
  }

  for (let i = 0; i < KEYS; i++) {
    checksum = (checksum * 31 + map.get(i)) % MOD;
  }
}

console.log(checksum);
