// compute-multi (rust entry) -- the compute-single workload split across
// available_parallelism() threads, each summing a contiguous chunk of
// 1..1,000,000; the partials are combined on the main thread.
//
// Build:  rustc -O main.rs -o compute-multi.exe
// Run:    compute-multi.exe
//
// Prints the same single integer as every other entry (single or multi), so
// the outputs cross-check each other.

use std::thread;

fn collatz_steps(start: u64) -> u64 {
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

fn sum_range(first: u64, last: u64) -> u64 {
    let mut total: u64 = 0;
    for start in first..=last {
        total += collatz_steps(start);
    }
    total
}

fn main() {
    let n: u64 = 1_000_000;
    let workers = thread::available_parallelism().map(|c| c.get()).unwrap_or(1) as u64;
    let chunk = (n + workers - 1) / workers;

    let mut pool = Vec::new();
    for w in 0..workers {
        let first = w * chunk + 1;
        let last = (n).min((w + 1) * chunk);
        pool.push(thread::spawn(move || sum_range(first, last)));
    }

    let mut total: u64 = 0;
    for worker in pool {
        total += worker.join().expect("worker panicked");
    }
    println!("{}", total);
}
