// records - variant events
import std;

struct E {
    kind: i32,
    id: i64,
    a: i32,
    b: i32,
    w: u8[..16],
}

fn xs_next(x: u64) -> u64 {
    var y = x;
    y = y ^ (y << 13);
    y = y ^ (y >> 7);
    y = y ^ (y << 17);
    return y;
}

fn main() {
    var log: E[>..] = [];
    var r: u64 = 12345;
    for i in 100000 {
        r = xs_next(r);
        let k = r % 8;
        let id: i64 = ((r >> 3) % 100000) as i64;
        if k < 4 {
            var e = E { kind: 1, id: id, a: 0, b: 0, w: [] };
            e.a = (((r >> 20) % 201) as i64 - 100) as i32;
            e.b = (((r >> 40) % 201) as i64 - 100) as i32;
            log.push(e);
        } else if k < 6 {
            var e = E { kind: 2, id: id, a: (4 + r % 12) as i32, b: (r % 26) as i32, w: [] };
            var w = r;
            for j in e.a {
                w = xs_next(w);
                e.w.push(('a' + (w % 26) as i64) as u8);
            }
            log.push(e);
        } else if k < 7 {
            log.push(E { kind: 3, id: id, a: ((r >> 20) % 256) as i32, b: 0, w: [] });
        } else {
            log.push(E { kind: 0, id: id, a: 0, b: 0, w: [] });
        }
    }

    var total: i64 = 0;
    for p in 4 {
        for e in log {
            if e.kind == 0 {
                total += 1;
            } else if e.kind == 1 {
                total += e.id + e.a + e.b;
            } else if e.kind == 2 {
                total += e.id + e.a + e.w[0];
            } else {
                total += e.id + e.a;
            }
        }
    }
    print(log.len);
    print(total);
}
