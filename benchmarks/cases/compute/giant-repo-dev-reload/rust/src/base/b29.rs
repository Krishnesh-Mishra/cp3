pub fn b29(x: i64) -> i64 {
    let mut acc = x + 29;
    acc = (acc * 31 + 203) % 1000033;
    acc = (acc * 131 + x * 17 + 29) % 1000199;
    acc
}
