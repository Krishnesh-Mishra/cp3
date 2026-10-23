// sha256-churn (js entry) -- hash one fixed 1 KiB buffer ITERATIONS times and
// fold the first four digest bytes into a checksum.
//
// Run:    node main.mjs
//
// Same workload and same output as the cp3, Go and Python entries; the C++ and
// Rust entries are omitted because neither standard library ships SHA-256 (see
// TASK_OMIT_LANGUAGES in bench/run_suite.py).
//
// The buffer is the seed repeated to exactly 1024 bytes and never changes, so
// every hash is over identical bytes; what the task measures is the hash
// implementation plus the per-call boundary. node:crypto's one-shot hash()
// returns the digest hex-encoded; its first eight hex digits are the first four
// digest bytes, parsed as one big-endian 32-bit integer.

import { hash } from "node:crypto";

const BUFFER_SIZE = 1024;
const ITERATIONS = 60000;
const SEED = "0123456789abcdef";

function sha256Churn(iterations) {
  const data = Buffer.from(SEED.repeat(BUFFER_SIZE / SEED.length));
  let checksum = 0;
  for (let k = 0; k < iterations; k++) {
    const digest = hash("sha256", data);
    checksum = (checksum * 31 + parseInt(digest.slice(0, 8), 16)) % 1000003;
  }
  return checksum;
}

console.log(sha256Churn(ITERATIONS));
