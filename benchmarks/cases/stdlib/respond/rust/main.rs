fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
fn main() {
    let mut out = String::new();
    let mut r: u64 = 12345;
    for _ in 0..10000u64 {
        r = xs_next(r); let id = r % 100000;
        r = xs_next(r); let len = 4 + (r % 9) as usize;
        let mut name = String::new();
        for j in 0..len { r = xs_next(r); if j % 8 == 7 { name.push(if r % 2 == 0 { '"' } else { '\\' }); } else { name.push((b'a' + (r % 26) as u8) as char); } }
        r = xs_next(r); let score = r % 1000; let active = (r >> 8) % 2 == 0;
        out.push_str("{\"id\":");
        out.push_str(&id.to_string());
        out.push_str(",\"name\":\"");
        for c in name.chars() { if c == '"' || c == '\\' { out.push('\\'); } out.push(c); }
        out.push_str("\",\"score\":");
        out.push_str(&score.to_string());
        out.push_str(",\"active\":");
        out.push_str(if active { "true" } else { "false" });
        out.push_str("},");
    }
    let mut h: u64 = 1469598103934665603;
    for b in out.as_bytes() { h = (h ^ (*b as u64)).wrapping_mul(1099511628211); }
    println!("{}", out.len()); println!("{}", h);
}