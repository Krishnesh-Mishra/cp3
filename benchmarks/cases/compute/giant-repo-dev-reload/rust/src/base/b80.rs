pub fn b80(x: i64) -> i64 {
    let mut acc = x + 80;
    acc = (acc * 31 + 560) % 1000033;
    acc = (acc * 131 + x * 17 + 80) % 1000199;
    acc
}
