pub fn b76(x: i64) -> i64 {
    let mut acc = x + 76;
    acc = (acc * 31 + 532) % 1000033;
    acc = (acc * 131 + x * 17 + 76) % 1000199;
    acc
}
