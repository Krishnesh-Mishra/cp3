pub fn b55(x: i64) -> i64 {
    let mut acc = x + 55;
    acc = (acc * 31 + 385) % 1000033;
    acc = (acc * 131 + x * 17 + 55) % 1000199;
    acc
}
