pub fn b31(x: i64) -> i64 {
    let mut acc = x + 31;
    acc = (acc * 31 + 217) % 1000033;
    acc = (acc * 131 + x * 17 + 31) % 1000199;
    acc
}
