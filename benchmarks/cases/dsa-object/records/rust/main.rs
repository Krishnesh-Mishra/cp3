fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
fn main() {
    struct E { kind: u64, id: i64, a: i64, b: i64, w0: u8, wlen: u64 }
    let mut log: Vec<E> = Vec::new();
    let mut r: u64 = 12345;
    for _ in 0..100000u64 {
        r = xs_next(r); let k = r % 8; let id = ((r >> 3) % 100000) as i64;
        let mut e = E { kind: 0, id, a: 0, b: 0, w0: 0, wlen: 0 };
        if k < 4 { e.kind = 1; e.a = (((r >> 20) % 201) as i64) - 100; e.b = (((r >> 40) % 201) as i64) - 100; }
        else if k < 6 { e.kind = 2; e.a = (r % 12) as i64; e.wlen = 4 + (r % 12); let mut w = xs_next(r); e.w0 = (w % 26) as u8 + b'a'; w = xs_next(w); let _ = w; }
        else if k < 7 { e.kind = 3; e.a = ((r >> 20) % 256) as i64; }
        log.push(e);
    }
    let mut total: i64 = 0;
    for _ in 0..4 { for e in &log { if e.kind == 0 { total += 1; } else if e.kind == 1 { total += e.id + e.a + e.b; } else if e.kind == 2 { total += e.id + e.wlen as i64 + e.w0 as i64; } else { total += e.id + e.a; } } }
    println!("{}", log.len()); println!("{}", total);
}