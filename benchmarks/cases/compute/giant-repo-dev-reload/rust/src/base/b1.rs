pub fn b1(x: i64) -> i64 {
    let mut acc = x + 1;
    acc = (acc * 31 + 7) % 1000033;
    acc = (acc * 131 + x * 17 + 1) % 1000199;
    acc
}
