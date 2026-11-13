pub fn b15(x: i64) -> i64 {
    let mut acc = x + 15;
    acc = (acc * 31 + 105) % 1000033;
    acc = (acc * 131 + x * 17 + 15) % 1000199;
    acc
}
