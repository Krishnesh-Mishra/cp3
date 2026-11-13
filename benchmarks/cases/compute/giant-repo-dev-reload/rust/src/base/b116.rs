pub fn b116(x: i64) -> i64 {
    let mut acc = x + 116;
    acc = (acc * 31 + 812) % 1000033;
    acc = (acc * 131 + x * 17 + 116) % 1000199;
    acc
}
