fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
fn main() {
    let n: i64 = 4_000_000; let passes: i64 = 8;
    let mut data: Vec<i32> = Vec::with_capacity(n as usize);
    let mut r: u64 = 12345;
    for _ in 0..n { r = xs_next(r); data.push((r % 1000) as i32); }
    let mut total: i64 = 0;
    for p in 0..passes { for &x in &data { total += (x ^ p as i32) as i64; } }
    println!("{}", n); println!("{}", total);
}
