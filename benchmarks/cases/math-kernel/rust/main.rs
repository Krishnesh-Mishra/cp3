// math-kernel (rust entry) -- evaluate sqrt, floor, abs, pow and clamp over
// 5,000,000 points, quantize each result to an integer and fold one checksum.
//
// Build:  rustc -O main.rs -o math-kernel.exe
// Run:    math-kernel.exe
//
// Same workload and same output in every language entry. sqrt is correctly
// rounded, powi uses an integer exponent, and every term is quantized to an
// integer before folding, so the checksum is exact across libms.

const COUNT: usize = 5_000_000;
const MOD: i64 = 1_000_000_007;

fn main() {
    let mut checksum: i64 = 0;
    let mut sqrt_total = 0.0_f64;
    for i in 1..=COUNT {
        let x = i as f64 * 0.000001;
        let s = (i as f64).sqrt();
        sqrt_total += s;
        let f = (x + 0.5).floor();
        let a = (x - 2.5).abs();
        let p = (i as f64).powi(2) as i64;
        let c = (x * 1000.0).clamp(0.0, 1000.0);
        let mixed = s + f + a + c;
        let quantum = (mixed * 1_000_000.0) as i64;
        checksum = (checksum * 31 + quantum + p) % MOD;
    }
    let sqrt_scaled = (sqrt_total * 1000.0) as i64;
    checksum = (checksum * 31 + sqrt_scaled) % MOD;
    println!("{}", checksum);
}
