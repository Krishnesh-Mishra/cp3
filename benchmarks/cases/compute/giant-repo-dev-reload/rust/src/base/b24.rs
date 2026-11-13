pub fn b24(x: i64) -> i64 {
    let mut acc = x + 24;
    acc = (acc * 31 + 168) % 1000033;
    acc = (acc * 131 + x * 17 + 24) % 1000199;
    acc
}
