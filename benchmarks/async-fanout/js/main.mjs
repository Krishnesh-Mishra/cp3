// async-fanout (js entry) -- two phases folded into one integer.
//
// Run: node main.mjs
//
// Phase 1: 10,000 zero-delay timers, all scheduled in one tick and joined at
//          once with Promise.all; each timer resolves with its index, and the
//          resolved values are summed.
// Phase 2: 10,000 sequential awaits of an already-resolved promise, folding
//          the loop index at every step (the coroutine/microtask churn probe).
//
// Node's timers and microtasks are the home-turf opponent this row exists for.
// The output is identical to the cp3, Python and Go entries.

const FANOUT = 10000;
const CHAIN = 10000;
const MOD = 1000003;

function tick(n) {
  return new Promise((resolve) => setTimeout(() => resolve(n), 0));
}

// One synchronous map schedules all 10,000 timers before any of them fires, so
// the join really is 10,000 concurrent zero-delay timers, not a trickle.
const values = await Promise.all(
  Array.from({ length: FANOUT }, (_, i) => tick(i)),
);

let checksum = values.reduce((sum, value) => sum + value, 0) % MOD;

for (let i = 0; i < CHAIN; i++) {
  const value = await Promise.resolve(i);
  checksum = (checksum * 31 + value) % MOD;
}

console.log(checksum);
