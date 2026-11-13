pub fn b52(x: i64) -> i64 {
    let mut acc = x + 52;
    acc = (acc * 31 + 364) % 1000033;
    acc = (acc * 131 + x * 17 + 52) % 1000199;
    acc
}
