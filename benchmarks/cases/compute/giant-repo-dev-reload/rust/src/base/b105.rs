pub fn b105(x: i64) -> i64 {
    let mut acc = x + 105;
    acc = (acc * 31 + 735) % 1000033;
    acc = (acc * 131 + x * 17 + 105) % 1000199;
    acc
}
