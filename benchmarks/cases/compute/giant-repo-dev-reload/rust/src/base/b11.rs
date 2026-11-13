pub fn b11(x: i64) -> i64 {
    let mut acc = x + 11;
    acc = (acc * 31 + 77) % 1000033;
    acc = (acc * 131 + x * 17 + 11) % 1000199;
    acc
}
