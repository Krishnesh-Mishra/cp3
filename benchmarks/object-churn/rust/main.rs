// object-churn (rust entry) -- create 5,000,000 small heap objects in a hot
// loop: each iteration's temporary dies in scope, and every second object
// escapes into a vector. One checksum at the end.
//
// Build:  rustc -O main.rs -o object-churn.exe
// Run:    object-churn.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). Box is the honest heap allocation; the temporary boxes are
// dropped every iteration which is what exercises the allocator and, for the
// kept half, the ownership bookkeeping.

struct Cell {
    value: i64,
}

fn main() {
    const OBJECTS: i64 = 5_000_000;
    const MOD: i64 = 1_000_000_007;

    let mut kept: Vec<Box<Cell>> = Vec::with_capacity((OBJECTS / 2) as usize);
    let mut acc: i64 = 0;
    for i in 0..OBJECTS {
        let temp = Box::new(Cell { value: i });
        acc = (acc + temp.value) % MOD;
        if i % 2 == 0 {
            kept.push(Box::new(Cell { value: i }));
        }
    }

    let mut checksum = acc;
    for cell in &kept {
        checksum = (checksum * 31 + cell.value) % MOD;
    }
    println!("{}", checksum);
}
