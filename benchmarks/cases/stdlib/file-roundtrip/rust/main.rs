// file-roundtrip (rust entry) -- write, read and stat one fixed 4 KiB file N
// times in the OS temp directory, then remove it.
//
// Build:  rustc -O main.rs -o file-roundtrip.exe
// Run:    file-roundtrip.exe
//
// Same workload and same output as the cp3, C++, Go, JavaScript and Python
// entries. The checksum folds the bytes read (length, byte sum, first and last
// byte) and the stat result (size, isFile, isDirectory), so a language that
// wrote a different file or mis-stated the metadata would print a different
// number.

use std::env;
use std::fs;

const ITERATIONS: u64 = 200;
const FILE_BYTES: usize = 4096;
const MOD: u64 = 1000003;

fn payload() -> Vec<u8> {
    const SEED: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    let mut data = String::new();
    while data.len() < FILE_BYTES {
        data.push_str(SEED);
    }
    data.truncate(FILE_BYTES);
    data.into_bytes()
}

fn main() {
    let data = payload();
    let file = env::temp_dir().join("cp3-bench-file-roundtrip.txt");

    let mut checksum: u64 = 0;
    for _ in 0..ITERATIONS {
        fs::write(&file, &data).expect("write failed");
        let read = fs::read(&file).expect("read failed");
        assert_eq!(read.len(), FILE_BYTES, "wrong file size");
        let metadata = fs::metadata(&file).expect("stat failed");

        let bytes: u64 = read.iter().map(|&b| b as u64).sum();
        checksum = (checksum * 31 + read.len() as u64) % MOD;
        checksum = (checksum * 31 + bytes) % MOD;
        checksum = (checksum * 31 + read[0] as u64) % MOD;
        checksum = (checksum * 31 + read[read.len() - 1] as u64) % MOD;
        checksum = (checksum * 31 + metadata.len()) % MOD;
        checksum = (checksum * 31 + if metadata.is_file() { 1 } else { 0 }) % MOD;
        checksum = (checksum * 31 + if metadata.is_dir() { 1 } else { 0 }) % MOD;
    }

    fs::remove_file(&file).expect("remove failed");
    println!("{}", checksum);
}
