// string-churn (js entry) -- build a 4 KiB string, then repeatedly rebuild
// strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.
//
// Run: node main.mjs [iterations]        (default 50000)
//
// Same workload and same output in every language entry. Every round allocates
// new strings, which is what makes this task exercise allocators and GCs. The
// per-iteration fold (checksum * 31 + first 'c' position + length) depends on
// the exact string contents, so all six languages must print the same number.

const DEFAULT_ITERATIONS = 50000;

const iterations = process.argv[2] !== undefined
  ? Number(process.argv[2])
  : DEFAULT_ITERATIONS;

const seed = "abcXYZ0123456789";
let data = "";
while (data.length < 4096) {
  data += seed;
}

let checksum = 0;
for (let k = 0; k < iterations; k++) {
  let s = data;
  for (let round = 0; round < 8; round++) {
    s = s.toUpperCase();
    s = s.slice(3, s.length - 1);
    s = s.toLowerCase() + s.slice(0, 8);
    s = s.slice(2);
  }
  checksum = (checksum * 31 + s.indexOf("c") + s.length) % 1000003;
}
console.log(checksum);
