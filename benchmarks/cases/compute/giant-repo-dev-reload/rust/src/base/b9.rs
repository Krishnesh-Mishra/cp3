pub fn b9(x: i64) -> i64 {
    let mut acc = x + 9;
    acc = (acc * 31 + 63) % 1000033;
    acc = (acc * 131 + x * 17 + 9) % 1000199;
    acc
}
