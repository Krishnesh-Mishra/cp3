pub fn b25(x: i64) -> i64 {
    let mut acc = x + 25;
    acc = (acc * 31 + 175) % 1000033;
    acc = (acc * 131 + x * 17 + 25) % 1000199;
    acc
}
