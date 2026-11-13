pub fn b12(x: i64) -> i64 {
    let mut acc = x + 12;
    acc = (acc * 31 + 84) % 1000033;
    acc = (acc * 131 + x * 17 + 12) % 1000199;
    acc
}
