pub fn b88(x: i64) -> i64 {
    let mut acc = x + 88;
    acc = (acc * 31 + 616) % 1000033;
    acc = (acc * 131 + x * 17 + 88) % 1000199;
    acc
}
