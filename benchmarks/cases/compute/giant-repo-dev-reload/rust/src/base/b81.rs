pub fn b81(x: i64) -> i64 {
    let mut acc = x + 81;
    acc = (acc * 31 + 567) % 1000033;
    acc = (acc * 131 + x * 17 + 81) % 1000199;
    acc
}
