pub fn b45(x: i64) -> i64 {
    let mut acc = x + 45;
    acc = (acc * 31 + 315) % 1000033;
    acc = (acc * 131 + x * 17 + 45) % 1000199;
    acc
}
