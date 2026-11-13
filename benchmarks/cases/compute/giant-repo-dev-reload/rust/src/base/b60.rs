pub fn b60(x: i64) -> i64 {
    let mut acc = x + 60;
    acc = (acc * 31 + 420) % 1000033;
    acc = (acc * 131 + x * 17 + 60) % 1000199;
    acc
}
