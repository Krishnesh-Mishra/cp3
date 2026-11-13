pub fn b61(x: i64) -> i64 {
    let mut acc = x + 61;
    acc = (acc * 31 + 427) % 1000033;
    acc = (acc * 131 + x * 17 + 61) % 1000199;
    acc
}
