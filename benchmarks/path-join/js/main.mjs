// path-join (js entry) -- join, normalize, basename and extname one fixed
// path-segment list N times and fold stable string lengths and characters.
//
// Run:    node main.mjs
//
// Same workload and same output as the cp3, C++, Rust, Go and Python entries.
// Only lengths and the first character of each result are folded, so the
// checksum is identical on Windows and POSIX (the separator is one character
// in every language's path surface).

import path from "node:path";

const ITERATIONS = 170000;
const MOD = 1000003;
const PARTS = ["var", "log", "cp3", "service", "events-2026-10-23.log"];

let checksum = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const joined = path.join(...PARTS);
  const normalized = path.normalize(joined);
  const base = path.basename(joined);
  const ext = path.extname(joined);

  checksum = (checksum * 31 + joined.length) % MOD;
  checksum = (checksum * 31 + normalized.length) % MOD;
  checksum = (checksum * 31 + base.length) % MOD;
  checksum = (checksum * 31 + ext.length) % MOD;
  checksum = (checksum * 31 + joined.charCodeAt(0)) % MOD;
  checksum = (checksum * 31 + base.charCodeAt(0)) % MOD;
  checksum = (checksum * 31 + ext.charCodeAt(0)) % MOD;
}
console.log(checksum);
