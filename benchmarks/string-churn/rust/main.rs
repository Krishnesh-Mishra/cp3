// string-churn (rust entry) -- build a 4 KiB string, then repeatedly rebuild
// strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.
//
// Build:  rustc -O main.rs -o string-churn.exe
// Run:    string-churn.exe [iterations]        (default 50000)
//
// Same workload and same output in every language entry. Every round allocates
// new strings, which is what makes this task exercise allocators and GCs. The
// per-iteration fold (checksum * 31 + first 'c' position + length) depends on
// the exact string contents, so all six languages must print the same number.

use std::env;

const DEFAULT_ITERATIONS: u64 = 50000;

fn main() {
    let iterations: u64 = env::args()
        .nth(1)
        .and_then(|text| text.parse().ok())
        .unwrap_or(DEFAULT_ITERATIONS);

    let seed = "abcXYZ0123456789";
    let mut data = String::new();
    while data.len() < 4096 {
        data.push_str(seed);
    }

    let mut checksum: u64 = 0;
    for _ in 0..iterations {
        let mut s = data.clone();
        for _ in 0..8 {
            let upper = s.to_uppercase();
            // Owned slice: this row measures string churn, so every entry pays
            // the same copy for the intermediate slice (a &str view here would
            // skip one allocation per round and make the comparison unfair).
            let sliced = upper[3..upper.len() - 1].to_string();
            let mut rebuilt = sliced.to_lowercase();
            rebuilt.push_str(&sliced[..8]);
            s = rebuilt[2..].to_string();
        }
        let position = s.find('c').map(|index| index as i64).unwrap_or(-1);
        let fold = position + s.len() as i64;
        checksum = (checksum * 31 + fold as u64) % 1000003;
    }
    println!("{}", checksum);
}
