// file-roundtrip (js entry) -- write, read and stat one fixed 4 KiB file N
// times in the OS temp directory, then remove it.
//
// Run:    node main.mjs
//
// Same workload and same output as the cp3, C++, Rust, Go and Python entries.
// The checksum folds the bytes read (length, byte sum, first and last byte)
// and the stat result (size, isFile, isDirectory), so a language that wrote a
// different file or mis-stated the metadata would print a different number.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const ITERATIONS = 200;
const FILE_BYTES = 4096;
const MOD = 1000003;

function payload() {
  const seed = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  let data = "";
  while (data.length < FILE_BYTES) {
    data += seed;
  }
  return data.slice(0, FILE_BYTES);
}

const data = payload();
const file = path.join(os.tmpdir(), "cp3-bench-file-roundtrip.txt");

let checksum = 0;
for (let i = 0; i < ITERATIONS; i++) {
  fs.writeFileSync(file, data);
  const read = fs.readFileSync(file);
  if (read.length !== FILE_BYTES) {
    throw new Error("wrong file size");
  }
  const stats = fs.statSync(file);

  let bytes = 0;
  for (let j = 0; j < read.length; j++) {
    bytes += read[j];
  }
  checksum = (checksum * 31 + read.length) % MOD;
  checksum = (checksum * 31 + bytes) % MOD;
  checksum = (checksum * 31 + read[0]) % MOD;
  checksum = (checksum * 31 + read[read.length - 1]) % MOD;
  checksum = (checksum * 31 + stats.size) % MOD;
  checksum = (checksum * 31 + (stats.isFile() ? 1 : 0)) % MOD;
  checksum = (checksum * 31 + (stats.isDirectory() ? 1 : 0)) % MOD;
}

fs.unlinkSync(file);
console.log(checksum);
