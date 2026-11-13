pub fn b96(x: i64) -> i64 {
    let mut acc = x + 96;
    acc = (acc * 31 + 672) % 1000033;
    acc = (acc * 131 + x * 17 + 96) % 1000199;
    acc
}
