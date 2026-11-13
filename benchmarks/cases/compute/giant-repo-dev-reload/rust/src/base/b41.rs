pub fn b41(x: i64) -> i64 {
    let mut acc = x + 41;
    acc = (acc * 31 + 287) % 1000033;
    acc = (acc * 131 + x * 17 + 41) % 1000199;
    acc
}
