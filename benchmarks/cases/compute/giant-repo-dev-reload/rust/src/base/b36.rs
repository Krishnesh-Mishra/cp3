pub fn b36(x: i64) -> i64 {
    let mut acc = x + 36;
    acc = (acc * 31 + 252) % 1000033;
    acc = (acc * 131 + x * 17 + 36) % 1000199;
    acc
}
