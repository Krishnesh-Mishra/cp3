pub fn b17(x: i64) -> i64 {
    let mut acc = x + 17;
    acc = (acc * 31 + 119) % 1000033;
    acc = (acc * 131 + x * 17 + 17) % 1000199;
    acc
}
