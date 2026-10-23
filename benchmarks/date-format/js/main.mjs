// date-format (js entry) -- construct UTC dates from one fixed epoch
// millisecond value, format the components manually, and fold a fixed diff.
//
// Run: node main.mjs
//
// Same workload, same fold and same output in every runnable language entry.
// The formatted spelling is built by hand from getUTC* components, so every
// language emits the same 19 ASCII characters: YYYY-MM-DDTHH:MM:SS.

const BASE_MS = 1700000000000; // 2023-11-14T22:13:20Z
const LATER_MS = 1700000123456; // +123,456 ms
const ITERATIONS = 50000;
const MOD = 1000003;
const HOUR_SECONDS = 3600;

// '0'..'9' -> 0..9, '-' -> 10, 'T' -> 11, ':' -> 12. The same weights are used
// by every entry, so the character fold cannot drift between languages.
function charValue(ch) {
  if (ch >= "0" && ch <= "9") return ch.charCodeAt(0) - "0".charCodeAt(0);
  if (ch === "-") return 10;
  if (ch === "T") return 11;
  return 12;
}

function pad(value, width) {
  return value.toString().padStart(width, "0");
}

let checksum = 0;
const diffMs = LATER_MS - BASE_MS;

for (let k = 0; k < ITERATIONS; k++) {
  const d = new Date(BASE_MS + (k % HOUR_SECONDS) * 1000);
  const text =
    `${pad(d.getUTCFullYear(), 4)}-${pad(d.getUTCMonth() + 1, 2)}` +
    `-${pad(d.getUTCDate(), 2)}T${pad(d.getUTCHours(), 2)}` +
    `:${pad(d.getUTCMinutes(), 2)}:${pad(d.getUTCSeconds(), 2)}`;

  let charSum = 0;
  for (let i = 0; i < text.length; i++) {
    charSum += charValue(text.charAt(i));
  }

  checksum = (checksum * 31 + charSum + text.length + diffMs) % MOD;
}

console.log(checksum);
