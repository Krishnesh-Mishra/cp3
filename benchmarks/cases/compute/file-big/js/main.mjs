// file-big (js entry) -- write an 8 MiB deterministic payload to the OS temp
// directory, read it back, and fold length, byte sum, first byte and last
// byte for 8 iterations.
//
// Run: node main.mjs
//
// Expected output: 779913

import { readFileSync, unlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const SIZE = 8388608;
const ITERATIONS = 8;
const MOD = 1000003;

const SEED =
  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const PAYLOAD = Buffer.from(SEED.repeat(SIZE / 64), "utf8");
const FILE_PATH = join(tmpdir(), "cp3-bench-file-big-js.bin");

let checksum = 0;
for (let iter = 0; iter < ITERATIONS; iter++) {
  writeFileSync(FILE_PATH, PAYLOAD);
  const read = readFileSync(FILE_PATH);

  let sum = 0;
  for (let i = 0; i < read.length; i++) {
    sum += read[i];
  }

  checksum = (checksum * 31 + read.length) % MOD;
  checksum = (checksum * 31 + sum) % MOD;
  checksum = (checksum * 31 + read[0]) % MOD;
  checksum = (checksum * 31 + read[read.length - 1]) % MOD;
}

unlinkSync(FILE_PATH);
console.log(checksum);
