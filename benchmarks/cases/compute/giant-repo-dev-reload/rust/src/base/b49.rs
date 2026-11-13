pub fn b49(x: i64) -> i64 {
    let mut acc = x + 49;
    acc = (acc * 31 + 343) % 1000033;
    acc = (acc * 131 + x * 17 + 49) % 1000199;
    acc
}
