pub fn b16(x: i64) -> i64 {
    let mut acc = x + 16;
    acc = (acc * 31 + 112) % 1000033;
    acc = (acc * 131 + x * 17 + 16) % 1000199;
    acc
}
