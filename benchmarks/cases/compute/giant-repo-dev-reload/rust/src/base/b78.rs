pub fn b78(x: i64) -> i64 {
    let mut acc = x + 78;
    acc = (acc * 31 + 546) % 1000033;
    acc = (acc * 131 + x * 17 + 78) % 1000199;
    acc
}
