pub fn b65(x: i64) -> i64 {
    let mut acc = x + 65;
    acc = (acc * 31 + 455) % 1000033;
    acc = (acc * 131 + x * 17 + 65) % 1000199;
    acc
}
