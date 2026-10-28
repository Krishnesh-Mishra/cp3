// compute-single-int (rust entry) -- total Collatz steps for every start value
// in 1..1,000,000, printed as one integer. Same workload and output as every
// other entry; this is the native-integer tier (D65): i64 throughout.
//
// Build:  rustc -O main.rs -o compute-single-int.exe
// Run:    compute-single-int.exe

fn collatz_steps(start: i64) -> i64 {
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
    let n: i64 = 1_000_000;
    let mut total: i64 = 0;
    for start in 1..=n {
        total += collatz_steps(start);
    }
    println!("{}", total);
}
