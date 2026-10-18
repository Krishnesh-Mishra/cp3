// closure-capture (js entry) -- two closure shapes back to back over a
// 10,000,000-element source:
//   A: map / filter / reduce with arrow functions that capture offset and cap
//   B: 1000 closures, each capturing its own key, stored in an array and
//      invoked after the creating scope has ended (escaping closures)
// Prints both sums on one line.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry (expected line in the
// task README). Both totals stay below 2^53 so the arithmetic is exact.

const SOURCE = 10_000_000;
const OFFSET = 7;
const CAP = 3;

const numbers = Array.from({ length: SOURCE }, (_, i) => i);

const totalMapReduce = numbers
  .map((x) => x + OFFSET)
  .filter((x) => x % CAP === 0)
  .reduce((acc, x) => acc + x, 0);

const closures = [];
for (let key = 0; key < 1000; key++) {
  closures.push((x) => x * key + OFFSET);
}

let totalEscaping = 0;
for (let index = 0; index < closures.length; index++) {
  for (let call = 0; call < 3000; call++) {
    totalEscaping += closures[index](index);
  }
}

console.log(totalMapReduce, totalEscaping);
