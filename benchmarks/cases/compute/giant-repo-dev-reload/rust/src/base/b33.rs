pub fn b33(x: i64) -> i64 {
    let mut acc = x + 33;
    acc = (acc * 31 + 231) % 1000033;
    acc = (acc * 131 + x * 17 + 33) % 1000199;
    acc
}
