fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
const MOD: i64 = 1000003;
fn gen_expr(seed: u64, corrupt: bool) -> String {
    let mut r = seed; let mut s = String::new();
    let ops: [char; 4] = ['+', '-', '*', '/'];
    r = xs_next(r); let terms = 3 + (r % 4) as usize;
    for i in 0..terms { if i > 0 { r = xs_next(r); s.push(ops[(r % 4) as usize]); } r = xs_next(r); let n = (r % 99) + 1; if n >= 10 { s.push(('0' as u8 + (n / 10) as u8) as char); s.push(('0' as u8 + (n % 10) as u8) as char); } else { s.push(('0' as u8 + n as u8) as char); } }
    if corrupt && s.len() > 4 { let pos = s.len() / 2; s.replace_range(pos..pos + 1, "x"); }
    s
}
struct P { s: Vec<u8>, i: usize, err: bool }
impl P {
    fn expr(&mut self) -> i64 { let mut v = self.term(); while !self.err && self.i < self.s.len() && (self.s[self.i] == b'+' || self.s[self.i] == b'-') { let op = self.s[self.i]; self.i += 1; let t = self.term(); v = if op == b'+' { v + t } else { v - t }; } v }
    fn term(&mut self) -> i64 { let mut v = self.factor(); while !self.err && self.i < self.s.len() && (self.s[self.i] == b'*' || self.s[self.i] == b'/') { let op = self.s[self.i]; self.i += 1; let f = self.factor(); if op == b'*' { v = v * f; } else { if f == 0 { self.err = true; break; } v = v / f; } } v }
    fn factor(&mut self) -> i64 { if self.i >= self.s.len() { self.err = true; return 0; } if self.s[self.i] == b'(' { self.i += 1; let v = self.expr(); if self.i >= self.s.len() || self.s[self.i] != b')' { self.err = true; return 0; } self.i += 1; return v; } if self.s[self.i] == b'-' { self.i += 1; return -self.factor(); } if self.s[self.i] >= b'0' && self.s[self.i] <= b'9' { let mut v: i64 = 0; while self.i < self.s.len() && self.s[self.i] >= b'0' && self.s[self.i] <= b'9' { v = v * 10 + (self.s[self.i] - b'0') as i64; self.i += 1; } return v; } self.err = true; 0 }
}
fn main() {
    let mut ok: i64 = 0; let mut errn: i64 = 0; let mut total: i64 = 0;
    for i in 0..5000u64 { let s = gen_expr(12345 + i, i % 4 == 3); let mut p = P { s: s.into_bytes(), i: 0, err: false }; let v = p.expr(); if p.err || p.i != p.s.len() { errn += 1; continue; } ok += 1; total = (total + v) % MOD; if total < 0 { total += MOD; } }
    println!("{}", ok); println!("{}", errn); println!("{}", total);
}