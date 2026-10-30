// exception-cost (js entry) -- 1,000,000 calls each through a throw/catch
// predicate and a sentinel-return predicate, with the same error branch rate,
// folded into two checksums printed on one line.
//
// Run: node main.mjs
//
// Expected output: 446742259 446742259

const ITERATIONS = 1000000;
const MOD = 1000000007;

function mayThrow(i) {
  if (i % 3 === 0) {
    throw new Error("boom");
  }
  return i;
}

function mayReturn(i) {
  if (i % 3 === 0) {
    return -1;
  }
  return i;
}

let thrown = 0;
for (let i = 0; i < ITERATIONS; i++) {
  try {
    const v = mayThrow(i);
    thrown = (thrown * 31 + v) % MOD;
  } catch (e) {
    thrown = (thrown * 31 + 1) % MOD;
  }
}

let returned = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const v = mayReturn(i);
  if (v < 0) {
    returned = (returned * 31 + 1) % MOD;
  } else {
    returned = (returned * 31 + v) % MOD;
  }
}

console.log(thrown + " " + returned);
