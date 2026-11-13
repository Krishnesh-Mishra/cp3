pub fn b75(x: i64) -> i64 {
    let mut acc = x + 75;
    acc = (acc * 31 + 525) % 1000033;
    acc = (acc * 131 + x * 17 + 75) % 1000199;
    acc
}
