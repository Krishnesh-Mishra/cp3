pub fn b53(x: i64) -> i64 {
    let mut acc = x + 53;
    acc = (acc * 31 + 371) % 1000033;
    acc = (acc * 131 + x * 17 + 53) % 1000199;
    acc
}
