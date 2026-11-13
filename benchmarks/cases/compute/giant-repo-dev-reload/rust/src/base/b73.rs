pub fn b73(x: i64) -> i64 {
    let mut acc = x + 73;
    acc = (acc * 31 + 511) % 1000033;
    acc = (acc * 131 + x * 17 + 73) % 1000199;
    acc
}
