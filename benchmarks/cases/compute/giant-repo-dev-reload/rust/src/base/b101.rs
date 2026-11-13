pub fn b101(x: i64) -> i64 {
    let mut acc = x + 101;
    acc = (acc * 31 + 707) % 1000033;
    acc = (acc * 131 + x * 17 + 101) % 1000199;
    acc
}
