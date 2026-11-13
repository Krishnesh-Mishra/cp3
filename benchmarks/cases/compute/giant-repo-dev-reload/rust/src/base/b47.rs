pub fn b47(x: i64) -> i64 {
    let mut acc = x + 47;
    acc = (acc * 31 + 329) % 1000033;
    acc = (acc * 131 + x * 17 + 47) % 1000199;
    acc
}
