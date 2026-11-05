// respond - JSON render + FNV
import std;

fn xs_next(x: u64) -> u64 {
    var y = x;
    y = y ^ (y << 13);
    y = y ^ (y >> 7);
    y = y ^ (y << 17);
    return y;
}

fn main() {
    var out: u8[>..] = [];
    var r: u64 = 12345;
    for i in 10000 {
        r = xs_next(r);
        let id = r % 100000;
        r = xs_next(r);
        let length = 4 + (r % 9) as i64;
        var name: u8[>..] = [];
        for j in length {
            r = xs_next(r);
            if j % 8 == 7 {
                if r % 2 == 0 { name.push('"'); } else { name.push('\\'); }
            } else {
                name.push(('a' + (r % 26) as i64) as u8);
            }
        }
        r = xs_next(r);
        let score = r % 1000;
        let active = (r >> 8) % 2 == 0;
        out.append("{\"id\":");
        format(out, id);
        out.append(",\"name\":\"");
        for c in name {
            if c == '"' || c == '\\' { out.push('\\'); }
            out.push(c);
        }
        out.append("\",\"score\":");
        format(out, score);
        out.append(",\"active\":");
        out.append(if active { "true" } else { "false" });
        out.append("},");
    }
    var h: u64 = 1469598103934665603;
    for c in out {
        h = (h ^ c) * 1099511628211;
    }
    print(out.len);
    print(h);
}
