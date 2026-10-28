// compute-single-int (js entry) -- total Collatz steps for every start value
// in 1..1,000,000, printed as one integer.
//
// Run: node main.mjs
//
// Same workload and same output as every other entry. JavaScript has no
// separate integer type: the honest JS row uses number (a double), and every
// intermediate value stays below 2^53 so it is exact. See the task README for
// why this row is kept as-is instead of being hidden behind BigInt.

function collatzSteps(start) {
  let n = start;
  let steps = 0;
  while (n !== 1) {
    if (n % 2 === 0) {
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    steps += 1;
  }
  return steps;
}

let total = 0;
for (let start = 1; start <= 1_000_000; start++) {
  total += collatzSteps(start);
}
console.log(total);
