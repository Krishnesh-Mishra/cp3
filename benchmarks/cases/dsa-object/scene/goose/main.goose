// scene - transform graph
import std;

struct Node {
    id: i64,
    tx: i64,
    ty: i64,
    tz: i64,
    rot: i64,
    kids: i64[..4],
}

var pool: Node[>..] = [];
var gcount: i64 = 0;

fn xs_next(x: u64) -> u64 {
    var y = x;
    y = y ^ (y << 13);
    y = y ^ (y >> 7);
    y = y ^ (y << 17);
    return y;
}

recursive fn build(depth: i64, seed: u64) -> i64 {
    let r = xs_next(seed);
    let idx = gcount;
    gcount += 1;
    pool.push(Node {
        id: idx,
        tx: (r % 9) as i64,
        ty: ((r >> 16) % 9) as i64,
        tz: ((r >> 24) % 9) as i64,
        rot: ((r >> 12) % 4) as i64,
        kids: [],
    });
    if depth == 0 { return idx; }
    let k = 1 + ((r >> 40) % 4);
    for i in k {
        let child = build(depth - 1, xs_next(r + i));
        pool[idx].kids.push(child);
    }
    return idx;
}

recursive fn animate(n: i64, phase: i64) {
    if pool[n].id % 7 == phase {
        pool[n].rot = (pool[n].rot + 1) % 4;
        pool[n].tx = (pool[n].tx + 1) % 9;
    }
    for i in pool[n].kids.len {
        let c = pool[n].kids[i];
        animate(c, phase);
    }
}

recursive fn gather(n: i64) -> i64 {
    var t = pool[n].tx + pool[n].ty + pool[n].tz;
    for i in pool[n].kids.len {
        let c = pool[n].kids[i];
        t += gather(c);
    }
    return t;
}

fn main() {
    let root = build(4, 12345);
    var total: i64 = 0;
    for f in 16 {
        animate(root, f % 7);
        total = (total + gather(root)) % 1000000007;
    }
    print(gcount);
    print(total);
}
