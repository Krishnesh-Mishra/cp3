// particles - f32 particle integration (typeSystem rust)
import std;

let N = 5000;
let STEPS = 50;

fn xs_next(x: u64) -> u64 {
    var y = x;
    y = y ^ (y << 13);
    y = y ^ (y >> 7);
    y = y ^ (y << 17);
    return y;
}

fn main() {
    let dt: f32 = 0.0625;
    let g: f32 = 8.0;
    var x: f32[N] = [0.0; N];
    var y: f32[N] = [0.0; N];
    var vx: f32[N] = [0.0; N];
    var vy: f32[N] = [0.0; N];

    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        x[i] = (r % 1600) * 0.0625;
        r = xs_next(r);
        y[i] = (r % 1600) * 0.0625;
        r = xs_next(r);
        vx[i] = ((r % 200) as i64 - 100) * 0.0625;
        r = xs_next(r);
        vy[i] = ((r % 200) as i64 - 100) * 0.0625;
    }

    for s in STEPS {
        for i in N {
            x[i] += vx[i] * dt;
            y[i] += vy[i] * dt;
            vy[i] -= g * dt;
            if y[i] < 0.0 {
                y[i] = -y[i];
                vy[i] = -vy[i] * 0.5;
            }
            if x[i] >= 100.0 { x[i] -= 100.0; }
            if x[i] < 0.0 { x[i] += 100.0; }
        }
    }

    var total: i64 = 0;
    for i in N {
        total += (x[i] * 100.0) as i64;
        total += (y[i] * 100.0) as i64;
        total += (vx[i] * 100.0) as i64;
        total += (vy[i] * 100.0) as i64;
    }
    total = total % 1000000007;
    if total < 0 { total += 1000000007; }
    print(N);
    print(total);
}
