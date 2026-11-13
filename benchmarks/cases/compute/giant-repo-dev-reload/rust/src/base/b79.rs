pub fn b79(x: i64) -> i64 {
    let mut acc = x + 79;
    acc = (acc * 31 + 553) % 1000033;
    acc = (acc * 131 + x * 17 + 79) % 1000199;
    acc
}
