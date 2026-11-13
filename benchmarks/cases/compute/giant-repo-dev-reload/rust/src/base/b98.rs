pub fn b98(x: i64) -> i64 {
    let mut acc = x + 98;
    acc = (acc * 31 + 686) % 1000033;
    acc = (acc * 131 + x * 17 + 98) % 1000199;
    acc
}
