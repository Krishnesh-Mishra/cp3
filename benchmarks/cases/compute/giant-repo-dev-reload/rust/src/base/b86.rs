pub fn b86(x: i64) -> i64 {
    let mut acc = x + 86;
    acc = (acc * 31 + 602) % 1000033;
    acc = (acc * 131 + x * 17 + 86) % 1000199;
    acc
}
