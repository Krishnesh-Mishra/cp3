pub fn b54(x: i64) -> i64 {
    let mut acc = x + 54;
    acc = (acc * 31 + 378) % 1000033;
    acc = (acc * 131 + x * 17 + 54) % 1000199;
    acc
}
