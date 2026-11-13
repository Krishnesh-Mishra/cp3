pub fn b100(x: i64) -> i64 {
    let mut acc = x + 100;
    acc = (acc * 31 + 700) % 1000033;
    acc = (acc * 131 + x * 17 + 100) % 1000199;
    acc
}
