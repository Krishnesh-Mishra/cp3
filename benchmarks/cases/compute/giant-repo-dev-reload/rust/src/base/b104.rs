pub fn b104(x: i64) -> i64 {
    let mut acc = x + 104;
    acc = (acc * 31 + 728) % 1000033;
    acc = (acc * 131 + x * 17 + 104) % 1000199;
    acc
}
