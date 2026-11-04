// sum-array control (same as bench upstream)
import rng;
let N = 4000000;
let PASSES = 8;
fn main() {
    var data: i32[>..] = [];
    var r: u64 = 12345;
    for i in N { r = xs_next(r); data.push(xs_mod(r, 1000) as i32); }
    var total = 0;
    for p in PASSES { for x in data { total += x ^ p; } }
    print(data.len);
    print(total);
}
