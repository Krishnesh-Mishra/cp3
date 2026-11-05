// calc - recursive-descent parse + eval
import std;

let MOD = 1000003;

var g_s: u8[>..] = [];
var g_pos: i64 = 0;
var g_err: bool = false;

fn xs_next(x: u64) -> u64 {
    var y = x;
    y = y ^ (y << 13);
    y = y ^ (y >> 7);
    y = y ^ (y << 17);
    return y;
}

fn gen_expr(seed: u64, corrupt: bool) -> u8[>..] {
    var r = seed;
    var s: u8[>..] = [];
    r = xs_next(r);
    let terms: i64 = (3 + r % 4) as i64;
    for i in terms {
        if i > 0 {
            r = xs_next(r);
            let op = r % 4;
            if op == 0 { s.push('+'); }
            else if op == 1 { s.push('-'); }
            else if op == 2 { s.push('*'); }
            else { s.push('/'); }
        }
        r = xs_next(r);
        let n: i64 = (r % 99) as i64 + 1;
        if n >= 10 {
            s.push((48 + n / 10) as u8);
            s.push((48 + n % 10) as u8);
        } else {
            s.push((48 + n) as u8);
        }
    }
    if corrupt && s.len > 4 { s[s.len / 2] = 'x'; }
    return s;
}

recursive fn parse_factor() -> i64 {
    if g_pos >= g_s.len { g_err = true; return 0; }
    let c = g_s[g_pos];
    if c == '(' {
        g_pos += 1;
        let v = parse_expr();
        if g_pos >= g_s.len || g_s[g_pos] != ')' { g_err = true; return 0; }
        g_pos += 1;
        return v;
    }
    if c == '-' { g_pos += 1; return -parse_factor(); }
    if c >= '0' && c <= '9' {
        var v: i64 = 0;
        while g_pos < g_s.len {
            let d = g_s[g_pos];
            if d < '0' || d > '9' { break; }
            v = v * 10 + (d - '0');
            g_pos += 1;
        }
        return v;
    }
    g_err = true;
    return 0;
}

fn parse_term() -> i64 {
    var v = parse_factor();
    while !g_err && g_pos < g_s.len {
        let c = g_s[g_pos];
        if c != '*' && c != '/' { break; }
        g_pos += 1;
        let f = parse_factor();
        if c == '*' {
            v = v * f;
        } else {
            if f == 0 { g_err = true; break; }
            v = v / f;
        }
    }
    return v;
}

recursive fn parse_expr() -> i64 {
    var v = parse_term();
    while !g_err && g_pos < g_s.len {
        let c = g_s[g_pos];
        if c != '+' && c != '-' { break; }
        g_pos += 1;
        let t = parse_term();
        if c == '+' { v = v + t; } else { v = v - t; }
    }
    return v;
}

fn main() {
    var ok: i64 = 0;
    var errn: i64 = 0;
    var total: i64 = 0;
    for i in 5000 {
        let corrupt = i % 4 == 3;
        g_s = gen_expr((12345 + i) as u64, corrupt);
        g_pos = 0;
        g_err = false;
        let v = parse_expr();
        if g_err || g_pos != g_s.len {
            errn += 1;
            continue;
        }
        ok += 1;
        total = (total + v) % MOD;
        if total < 0 { total += MOD; }
    }
    print(ok);
    print(errn);
    print(total);
}
