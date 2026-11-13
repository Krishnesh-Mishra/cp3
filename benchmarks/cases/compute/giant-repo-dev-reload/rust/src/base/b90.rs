pub fn b90(x: i64) -> i64 {
    let mut acc = x + 90;
    acc = (acc * 31 + 630) % 1000033;
    acc = (acc * 131 + x * 17 + 90) % 1000199;
    acc
}
