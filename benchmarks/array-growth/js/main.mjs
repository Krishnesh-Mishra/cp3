// array-growth (js entry) -- push 20,000,000 numbers into a growing array
// while summing them, pop half of them back off, then print the sum.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry (expected value in the
// task README). The sum is 2.0000001e14, below 2^53, so the double-based
// arithmetic stays exact.

const PUSHES = 20_000_000;

const values = [];
let total = 0;
for (let i = 1; i <= PUSHES; i++) {
  values.push(i);
  total += i;
}
for (let k = 0; k < PUSHES / 2; k++) {
  values.pop();
}
console.log(total);
