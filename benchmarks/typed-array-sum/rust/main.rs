// typed-array-sum (rust entry) -- fill a fixed Float64Array and Uint8Array,
// sum the floats for 32 rounds, then fold the float and byte totals into one
// integer checksum.
//
// Build:  rustc -O main.rs -o typed-array-sum.exe
// Run:    typed-array-sum.exe
//
// Same workload and same output in every language entry. The float pattern is
// a multiple of 0.25 and every partial sum stays far below 2^53, so the
// addition is exact in IEEE-754 doubles.

const SIZE: usize = 1_000_000;
const ROUNDS: usize = 32;
const MOD: i64 = 1_000_000_007;

fn main() {
    let mut values = vec![0.0_f64; SIZE];
    let mut bytes = vec![0_u8; SIZE];
    for i in 0..SIZE {
        values[i] = (i % 97) as f64 * 0.5 + (i % 13) as f64 * 0.25;
        bytes[i] = ((i * 31 + 7) % 256) as u8;
    }

    let mut total = 0.0_f64;
    for _ in 0..ROUNDS {
        let mut acc = 0.0_f64;
        for value in &values {
            acc += *value;
        }
        total += acc;
    }

    let byte_total: i64 = bytes.iter().map(|value| *value as i64).sum();
    let scaled = (total * 4.0) as i64;
    let mut checksum = (scaled + byte_total) % MOD;
    checksum = (checksum * 31 + SIZE as i64 + ROUNDS as i64) % MOD;
    println!("{}", checksum);
}
