pub fn b119(x: i64) -> i64 {
    let mut acc = x + 119;
    acc = (acc * 31 + 833) % 1000033;
    acc = (acc * 131 + x * 17 + 119) % 1000199;
    acc
}
