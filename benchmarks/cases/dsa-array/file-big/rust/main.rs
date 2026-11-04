// file-big (rust entry) -- build an 8 MiB payload by repeating a 64-byte seed
// 131,072 times, then write and read it back 8 times in the OS temp directory,
// folding length, byte sum, first byte and last byte each iteration.
//
// Build:  rustc -C opt-level=3 main.rs -o file-big.exe
// Run:    file-big.exe
//
// Expected output: 779913

use std::env;
use std::fs;

const SEED: &[u8] = b"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const SIZE: usize = 8_388_608;
const ITERATIONS: usize = 8;
const MOD: u64 = 1_000_003;

fn main() {
    let payload = SEED.repeat(SIZE / SEED.len());
    let path = env::temp_dir().join("cp3-bench-file-big-rust.bin");

    let mut checksum: u64 = 0;
    for _ in 0..ITERATIONS {
        fs::write(&path, &payload).expect("write failed");
        let read = fs::read(&path).expect("read failed");

        let sum: u64 = read.iter().map(|&b| b as u64).sum();
        checksum = (checksum * 31 + read.len() as u64) % MOD;
        checksum = (checksum * 31 + sum) % MOD;
        checksum = (checksum * 31 + read[0] as u64) % MOD;
        checksum = (checksum * 31 + read[read.len() - 1] as u64) % MOD;
    }

    let _ = fs::remove_file(&path);
    println!("{}", checksum);
}
