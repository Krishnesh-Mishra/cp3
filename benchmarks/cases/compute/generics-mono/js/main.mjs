// generics-mono (js entry) -- the same 2,000,000-iteration sum run twice:
// once through a box class plus a passthrough function, once through a
// hand-written second class. Prints both sums on one line.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry (expected line in the
// task README). JavaScript has no type parameters: both loops are plain
// classes and the V8 JIT sees the same optimizable shape for each, so the
// ratio between the two loops is expected to hover at 1.0 for this row; that
// is the honest reading for a language where monomorphization does not exist.

class Box {
  constructor(value) {
    this.value = value;
  }

  get() {
    return this.value;
  }
}

function sumBox(box) {
  return box.get();
}

class NumberBox {
  constructor(value) {
    this.value = value;
  }

  get() {
    return this.value;
  }
}

const ITERATIONS = 2_000_000;
const MOD = 1_000_000_007;

let totalGeneric = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const box = new Box(i);
  totalGeneric = (totalGeneric + sumBox(box)) % MOD;
}

let totalConcrete = 0;
for (let i = 0; i < ITERATIONS; i++) {
  const box = new NumberBox(i);
  totalConcrete = (totalConcrete + box.get()) % MOD;
}

console.log(totalGeneric, totalConcrete);
