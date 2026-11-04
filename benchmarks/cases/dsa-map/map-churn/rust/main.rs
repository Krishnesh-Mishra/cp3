// map-churn (rust entry) -- fill a 100,000-entry i64 -> i64 HashMap, then run
// two rounds of delete / reinsert / in-place update churn and fold every key
// in ascending key order.
//
// Build:  rustc -C opt-level=3 main.rs -o map-churn.exe
// Run:    map-churn.exe
//
// Expected output: 942471769

use std::collections::HashMap;

fn main() {
    const KEYS: i64 = 100_000;
    const ROUNDS: i64 = 2;
    const MOD: i64 = 1_000_000_007;

    let mut map: HashMap<i64, i64> = HashMap::with_capacity(KEYS as usize);
    for i in 0..KEYS {
        map.insert(i, i * 3 + 1);
    }

    let mut checksum: i64 = 0;
    for round in 0..ROUNDS {
        // Delete every 10th key.
        for i in (0..KEYS).step_by(10) {
            map.remove(&i);
        }

        // Reinsert the deleted keys with a new value.
        for i in (0..KEYS).step_by(10) {
            map.insert(i, i * 5 + round + 2);
        }

        // Update every 7th key in place.
        for i in (0..KEYS).step_by(7) {
            *map.get_mut(&i).unwrap() += 1;
        }

        // Fold every key in ascending numeric order.
        for i in 0..KEYS {
            checksum = (checksum * 31 + map[&i]) % MOD;
        }
    }

    println!("{}", checksum);
}
