// buffer-copy (js entry) -- build a 64 KiB byte-pattern buffer, then reshape
// it ITERATIONS times: rotate it by a changing offset, overwrite the new first
// byte with a cycling marker, and fold a 96-byte window into a checksum.
//
// Run:    node main.mjs
//
// The other five entries run the same reshape with their own buffer types
// (cp3 Buffer, std::vector<uint8_t>, Vec<u8>, []byte, bytearray). Every
// intermediate buffer is exactly SIZE bytes, and every entry folds the same two
// marker positions, so all six languages must print the same number.
//
// Fairness note: Node's Buffer.slice/subarray return views over the same
// memory, so the rotation uses Buffer.concat, which allocates and copies the
// new SIZE-byte buffer exactly like the other entries' reassembly. Using
// slice() here would skip the copy the task is about.

const SIZE = 65536;
const ITERATIONS = 5000;
const WINDOW = 96;
const SEED = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?";

function bufferCopyChecksum(iterations) {
  let buf = Buffer.from(SEED.repeat(SIZE / SEED.length));
  let checksum = 0;

  for (let k = 0; k < iterations; k++) {
    const offset = (k * 4093 + 17) % SIZE;
    const marker = SEED.charCodeAt(k % SEED.length);

    // Rotate left by offset and overwrite the new first byte with marker.
    buf = Buffer.concat([
      Buffer.from([marker]),
      buf.subarray(offset + 1),
      buf.subarray(0, offset),
    ]);

    const window = buf.subarray(0, WINDOW);
    const firstA = window.indexOf(0x61) + 1;
    const first7 = window.indexOf(0x37) + 1;
    checksum = (checksum * 131 + buf.length + firstA + first7) % 1000003;
  }
  return checksum;
}

console.log(bufferCopyChecksum(ITERATIONS));
