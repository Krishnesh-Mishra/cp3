pub fn b3(x: i64) -> i64 {
    let mut acc = x + 3;
    acc = (acc * 31 + 21) % 1000033;
    acc = (acc * 131 + x * 17 + 3) % 1000199;
    acc
}
