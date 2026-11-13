pub fn b68(x: i64) -> i64 {
    let mut acc = x + 68;
    acc = (acc * 31 + 476) % 1000033;
    acc = (acc * 131 + x * 17 + 68) % 1000199;
    acc
}
