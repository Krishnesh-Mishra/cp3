pub fn b50(x: i64) -> i64 {
    let mut acc = x + 50;
    acc = (acc * 31 + 350) % 1000033;
    acc = (acc * 131 + x * 17 + 50) % 1000199;
    acc
}
