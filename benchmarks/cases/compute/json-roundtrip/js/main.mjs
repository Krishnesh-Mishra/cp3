// json-roundtrip (js entry) -- 20,000 iterations of JSON.parse + JSON.stringify
// of one representative API record, folded into one integer.
//
// Run: node main.mjs
//
// Same payload text, same field fold and same output as the cp3, Python and Go
// entries. V8's JSON.parse/stringify are the C++ fast path in the runtime, so
// this row is the scripted-runtime pressure the D67 gate exists for.
//
// Every length in the fold is a character count of ASCII text, so V8's UTF-16
// length and the byte counts used by Go and cp3 agree exactly.

const TEXT =
  '{"id":48271,"name":"Ada Lovelace","email":"ada.lovelace@example.com","active":true,"score":93.5,"title":"Principal Engineer","company":"Analytical Engines Ltd","city":"London","country":"GB","tags":["analyst","engineer","pioneer","mathematician","compiler","runtime"],"samples":[12,17,23,29,31,37,41,43,47,53]}';
const ITERATIONS = 20000;
const MOD = 1000003;

let checksum = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const u = JSON.parse(TEXT);
  const out = JSON.stringify({
    id: u.id,
    name: u.name,
    email: u.email,
    active: u.active,
    score: u.score,
    title: u.title,
    company: u.company,
    city: u.city,
    country: u.country,
    tags: u.tags,
    samples: u.samples,
  });

  let fieldValue = u.id + u.name.length + u.email.length + (u.active ? 1 : 0);
  fieldValue += u.score * 10 + u.title.length + u.company.length;
  fieldValue += u.city.length + u.country.length;
  fieldValue += u.tags.length + u.samples.length;
  for (const tag of u.tags) {
    fieldValue += tag.length;
  }
  for (const sample of u.samples) {
    fieldValue += sample;
  }

  checksum = (checksum * 31 + fieldValue + TEXT.length + out.length) % MOD;
}

console.log(checksum);
