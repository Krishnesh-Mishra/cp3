pub fn b28(x: i64) -> i64 {
    let mut acc = x + 28;
    acc = (acc * 31 + 196) % 1000033;
    acc = (acc * 131 + x * 17 + 28) % 1000199;
    acc
}
