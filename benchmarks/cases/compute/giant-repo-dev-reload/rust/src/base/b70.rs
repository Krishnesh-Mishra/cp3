pub fn b70(x: i64) -> i64 {
    let mut acc = x + 70;
    acc = (acc * 31 + 490) % 1000033;
    acc = (acc * 131 + x * 17 + 70) % 1000199;
    acc
}
