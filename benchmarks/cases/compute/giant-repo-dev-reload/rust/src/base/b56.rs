pub fn b56(x: i64) -> i64 {
    let mut acc = x + 56;
    acc = (acc * 31 + 392) % 1000033;
    acc = (acc * 131 + x * 17 + 56) % 1000199;
    acc
}
