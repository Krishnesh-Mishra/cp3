pub fn b69(x: i64) -> i64 {
    let mut acc = x + 69;
    acc = (acc * 31 + 483) % 1000033;
    acc = (acc * 131 + x * 17 + 69) % 1000199;
    acc
}
