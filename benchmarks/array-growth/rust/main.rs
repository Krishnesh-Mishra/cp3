// array-growth (rust entry) -- push 20,000,000 numbers into a growing vector
// while summing them, pop half of them back off, then print the sum.
//
// Build:  rustc -O main.rs -o array-growth.exe
// Run:    array-growth.exe
//
// Same workload and same output in every language entry (expected value in the
// task README). The Vec is deliberately not pre-reserved: doubling growth and
// the final shrink are the workload.

fn main() {
    const PUSHES: i64 = 20_000_000;
    let mut values: Vec<i64> = Vec::new();
    let mut total: i64 = 0;
    for i in 1..=PUSHES {
        values.push(i);
        total += i;
    }
    for _ in 0..PUSHES / 2 {
        values.pop();
    }
    println!("{}", total);
}
