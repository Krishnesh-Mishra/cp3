// env-injection (js entry) -- read one injected environment variable N times
// and fold its bytes into a checksum.
//
// Run:    node main.mjs        (CP3_BENCH_ENV must be set)
//
// The suite runner injects CP3_BENCH_ENV into every child process for this
// task. Each iteration performs a fresh process.env read (the lookup itself is
// the workload) and folds the value's length, byte sum, first byte and last
// byte into the checksum. A missing variable is an error, so a run without the
// injection fails instead of measuring an empty string.
//
// Same workload and same output as the cp3, C++, Rust, Go and Python entries.

const ITERATIONS = 500000;
const KEY = "CP3_BENCH_ENV";
const MOD = 1000003;

let checksum = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const value = process.env[KEY];
  if (value === undefined) {
    console.error("CP3_BENCH_ENV is not set");
    process.exit(1);
  }
  let bytes = 0;
  for (let j = 0; j < value.length; j++) {
    bytes += value.charCodeAt(j);
  }
  checksum = (checksum * 31 + value.length) % MOD;
  checksum = (checksum * 31 + bytes) % MOD;
  checksum = (checksum * 31 + value.charCodeAt(0)) % MOD;
  checksum = (checksum * 31 + value.charCodeAt(value.length - 1)) % MOD;
}
console.log(checksum);
