pub fn b5(x: i64) -> i64 {
    let mut acc = x + 5;
    acc = (acc * 31 + 35) % 1000033;
    acc = (acc * 131 + x * 17 + 5) % 1000199;
    acc
}
