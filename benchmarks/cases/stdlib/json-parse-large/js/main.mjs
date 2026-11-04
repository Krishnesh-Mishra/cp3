// json-parse-large (js entry) -- build a 204,472-byte JSON record with 50,000
// numbers and 1,000 tags, parse it 20 times with JSON.parse, and fold the
// parsed fields into one checksum.
//
// Run: node main.mjs
//
// Expected output: 613781

const NUMBERS = 50000;
const TAGS = 1000;
const ITERATIONS = 20;
const MOD = 1000003;

const numberParts = new Array(NUMBERS);
for (let i = 0; i < NUMBERS; i++) {
  numberParts[i] = String((i * 17 + 3) % 1000);
}

const tagParts = new Array(TAGS);
for (let i = 0; i < TAGS; i++) {
  tagParts[i] = '"tag-' + i + '"';
}

const TEXT =
  '{"id":48271,"name":"Ada Lovelace","active":true,"score":93.5,"numbers":[' +
  numberParts.join(",") +
  '],"tags":[' +
  tagParts.join(",") +
  "]}";

let checksum = 0;
for (let iter = 0; iter < ITERATIONS; iter++) {
  const payload = JSON.parse(TEXT);

  let field = payload.id + payload.name.length + (payload.active ? 1 : 0);
  field += Math.trunc(payload.score * 10);
  field += payload.numbers.length + payload.tags.length;

  for (const n of payload.numbers) {
    field += n;
  }
  for (const tag of payload.tags) {
    field += tag.length;
  }

  checksum = (checksum * 31 + field + TEXT.length) % MOD;
}

console.log(checksum);
