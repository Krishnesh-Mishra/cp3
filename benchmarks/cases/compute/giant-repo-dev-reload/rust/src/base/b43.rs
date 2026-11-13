pub fn b43(x: i64) -> i64 {
    let mut acc = x + 43;
    acc = (acc * 31 + 301) % 1000033;
    acc = (acc * 131 + x * 17 + 43) % 1000199;
    acc
}
