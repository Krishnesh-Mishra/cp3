pub fn b22(x: i64) -> i64 {
    let mut acc = x + 22;
    acc = (acc * 31 + 154) % 1000033;
    acc = (acc * 131 + x * 17 + 22) % 1000199;
    acc
}
