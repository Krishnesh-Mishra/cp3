pub fn b93(x: i64) -> i64 {
    let mut acc = x + 93;
    acc = (acc * 31 + 651) % 1000033;
    acc = (acc * 131 + x * 17 + 93) % 1000199;
    acc
}
