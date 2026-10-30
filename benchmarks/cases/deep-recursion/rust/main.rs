// deep-recursion (rust entry) -- recurse to depth 5,000 with a plain
// non-tail-recursive sum_to, then fold the triangular sum into a checksum
// over 200 rounds.
//
// Build:  rustc -C opt-level=3 main.rs -o deep-recursion.exe
// Run:    deep-recursion.exe
//
// Expected output: 292249866

fn sum_to(n: i64) -> i64 {
    if n == 0 {
        0
    } else {
        n + sum_to(n - 1)
    }
}

fn main() {
    const DEPTH: i64 = 5000;
    const ROUNDS: i64 = 200;
    const MOD: i64 = 1_000_000_007;

    let mut checksum: i64 = 0;
    for _ in 0..ROUNDS {
        checksum = (checksum * 31 + sum_to(DEPTH)) % MOD;
    }

    println!("{}", checksum);
}
