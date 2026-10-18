// generics-mono (rust entry) -- the same 2,000,000-iteration sum run twice:
// once through a generic wrapper type plus a generic function, once through a
// hand-written concrete type. Prints both sums on one line.
//
// Build:  rustc -O main.rs -o generics-mono.exe
// Run:    generics-mono.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). The generic type is named Boxed to avoid colliding with the
// std Box; the generic function is monomorphized per instantiation, and the
// concrete type is a separate hand-written struct, so the two loops measure
// what monomorphization buys against the same computation.

struct Boxed<T> {
    value: T,
}

impl<T: Copy> Boxed<T> {
    fn get(&self) -> T {
        self.value
    }
}

fn sum_box<T: Copy>(boxed: &Boxed<T>) -> T {
    boxed.get()
}

struct NumberBox {
    value: i64,
}

impl NumberBox {
    fn get(&self) -> i64 {
        self.value
    }
}

fn main() {
    const ITERATIONS: i64 = 2_000_000;
    const MOD: i64 = 1_000_000_007;

    let mut total_generic: i64 = 0;
    for i in 0..ITERATIONS {
        let boxed = Boxed { value: i };
        total_generic = (total_generic + sum_box(&boxed)) % MOD;
    }

    let mut total_concrete: i64 = 0;
    for i in 0..ITERATIONS {
        let boxed = NumberBox { value: i };
        total_concrete = (total_concrete + boxed.get()) % MOD;
    }

    println!("{} {}", total_generic, total_concrete);
}
