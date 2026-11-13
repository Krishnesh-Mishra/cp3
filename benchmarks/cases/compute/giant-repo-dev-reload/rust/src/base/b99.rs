pub fn b99(x: i64) -> i64 {
    let mut acc = x + 99;
    acc = (acc * 31 + 693) % 1000033;
    acc = (acc * 131 + x * 17 + 99) % 1000199;
    acc
}
