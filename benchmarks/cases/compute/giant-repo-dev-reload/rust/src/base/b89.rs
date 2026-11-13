pub fn b89(x: i64) -> i64 {
    let mut acc = x + 89;
    acc = (acc * 31 + 623) % 1000033;
    acc = (acc * 131 + x * 17 + 89) % 1000199;
    acc
}
