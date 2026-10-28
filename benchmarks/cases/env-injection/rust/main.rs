// env-injection (rust entry) -- read one injected environment variable N times
// and fold its bytes into a checksum.
//
// Build:  rustc -O main.rs -o env-injection.exe
// Run:    env-injection.exe        (CP3_BENCH_ENV must be set)
//
// The suite runner injects CP3_BENCH_ENV into every child process for this
// task. Each iteration performs a fresh env::var read (the lookup itself is
// the workload) and folds the value's length, byte sum, first byte and last
// byte into the checksum. A missing variable is an error, so a run without the
// injection fails instead of measuring an empty string.
//
// Same workload and same output as the cp3, C++, Go, JavaScript and Python
// entries.

use std::env;
use std::process;

const ITERATIONS: u64 = 500000;
const KEY: &str = "CP3_BENCH_ENV";
const MOD: u64 = 1000003;

fn main() {
    let mut checksum: u64 = 0;
    for _ in 0..ITERATIONS {
        let value = match env::var(KEY) {
            Ok(value) => value,
            Err(_) => {
                eprintln!("CP3_BENCH_ENV is not set");
                process::exit(1);
            }
        };
        let bytes: u64 = value.bytes().map(|b| b as u64).sum();
        checksum = (checksum * 31 + value.len() as u64) % MOD;
        checksum = (checksum * 31 + bytes) % MOD;
        checksum = (checksum * 31 + value.as_bytes()[0] as u64) % MOD;
        checksum = (checksum * 31 + value.as_bytes()[value.len() - 1] as u64) % MOD;
    }
    println!("{}", checksum);
}
