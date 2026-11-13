pub fn b10(x: i64) -> i64 {
    let mut acc = x + 10;
    acc = (acc * 31 + 70) % 1000033;
    acc = (acc * 131 + x * 17 + 10) % 1000199;
    acc
}
