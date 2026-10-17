// compute-single (rust entry) -- total Collatz steps for every start value in
// 1..1,000,000, printed as one integer.
//
// Build:  rustc -O main.rs -o compute-single.exe
// Run:    compute-single.exe
//
// Same workload and same output in every language entry.

fn collatz_steps(start: u64) -> u64 {
    let mut n = start;
    let mut steps = 0;
    while n != 1 {
        if n % 2 == 0 {
            n /= 2;
        } else {
            n = 3 * n + 1;
        }
        steps += 1;
    }
    steps
}

fn main() {
    let n: u64 = 1_000_000;
    let mut total: u64 = 0;
    for start in 1..=n {
        total += collatz_steps(start);
    }
    println!("{}", total);
}
