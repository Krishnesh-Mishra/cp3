pub fn b14(x: i64) -> i64 {
    let mut acc = x + 14;
    acc = (acc * 31 + 98) % 1000033;
    acc = (acc * 131 + x * 17 + 14) % 1000199;
    acc
}
