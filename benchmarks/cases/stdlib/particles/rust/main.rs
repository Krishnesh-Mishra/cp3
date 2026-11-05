fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
fn main() {
    const N: usize = 5000; const STEPS: usize = 50;
    let dt: f32 = 0.0625; let g: f32 = 8.0;
    let mut x = vec![0f32; N]; let mut y = vec![0f32; N]; let mut vx = vec![0f32; N]; let mut vy = vec![0f32; N];
    let mut r: u64 = 12345;
    for i in 0..N { r = xs_next(r); x[i] = (r % 1600) as f32 * 0.0625; r = xs_next(r); y[i] = (r % 1600) as f32 * 0.0625; r = xs_next(r); vx[i] = ((r % 200) as i64 - 100) as f32 * 0.0625; r = xs_next(r); vy[i] = ((r % 200) as i64 - 100) as f32 * 0.0625; }
    for _ in 0..STEPS { for i in 0..N { x[i] += vx[i] * dt; y[i] += vy[i] * dt; vy[i] -= g * dt; if y[i] < 0.0 { y[i] = -y[i]; vy[i] = -vy[i] * 0.5; } if x[i] >= 100.0 { x[i] -= 100.0; } if x[i] < 0.0 { x[i] += 100.0; } } }
    let mut total: i64 = 0; for i in 0..N { total += (x[i] * 100.0) as i64 + (y[i] * 100.0) as i64 + (vx[i] * 100.0) as i64 + (vy[i] * 100.0) as i64; }
    total %= 1000000007; if total < 0 { total += 1000000007; }
    println!("{}", N); println!("{}", total);
}