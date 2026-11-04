// object-churn (js entry) -- create 5,000,000 small objects in a hot loop:
// each iteration's temporary dies in scope, and every second object escapes
// into an array. One checksum at the end.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry (expected value in the
// task README). Every intermediate stays below 2^53 so the arithmetic is
// exact; the kept half is real garbage-collector pressure.

class Cell {
  constructor(value) {
    this.value = value;
  }
}

const OBJECTS = 5_000_000;
const MOD = 1_000_000_007;

const kept = [];
let acc = 0;
for (let i = 0; i < OBJECTS; i++) {
  const temp = new Cell(i);
  acc = (acc + temp.value) % MOD;
  if (i % 2 === 0) {
    kept.push(new Cell(i));
  }
}

let checksum = acc;
for (const cell of kept) {
  checksum = (checksum * 31 + cell.value) % MOD;
}
console.log(checksum);
