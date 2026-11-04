// path-join (rust entry) -- join, normalize, basename and extname one fixed
// path-segment list N times and fold stable string lengths and characters.
//
// Build:  rustc -O main.rs -o path-join.exe
// Run:    path-join.exe
//
// Same workload and same output as the cp3, C++, Go, JavaScript and Python
// entries. Only lengths and the first character of each result are folded, so
// the checksum is identical on Windows and POSIX (the separator is one
// character in every language's path surface).

use std::path::{Component, Path, PathBuf};

const ITERATIONS: u64 = 170000;
const MOD: u64 = 1000003;
const PARTS: [&str; 5] = ["var", "log", "cp3", "service", "events-2026-10-23.log"];

/// Lexical normalization with the standard component walk: drop `.`, resolve
/// `..` against the previous component, and leave everything else in place.
fn normalize(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other.as_os_str()),
        }
    }
    if out.as_os_str().is_empty() {
        out.push(".");
    }
    out
}

fn main() {
    let mut checksum: u64 = 0;
    for _ in 0..ITERATIONS {
        let mut joined = PathBuf::new();
        for part in PARTS {
            joined.push(part);
        }
        let normalized = normalize(&joined);
        let base = joined.file_name().expect("basename").to_string_lossy();
        let ext = match joined.extension() {
            Some(ext) => format!(".{}", ext.to_string_lossy()),
            None => String::new(),
        };

        let joined_text = joined.to_string_lossy();
        checksum = (checksum * 31 + joined_text.len() as u64) % MOD;
        checksum = (checksum * 31 + normalized.to_string_lossy().len() as u64) % MOD;
        checksum = (checksum * 31 + base.len() as u64) % MOD;
        checksum = (checksum * 31 + ext.len() as u64) % MOD;
        checksum = (checksum * 31 + joined_text.as_bytes()[0] as u64) % MOD;
        checksum = (checksum * 31 + base.as_bytes()[0] as u64) % MOD;
        checksum = (checksum * 31 + ext.as_bytes()[0] as u64) % MOD;
    }
    println!("{}", checksum);
}
