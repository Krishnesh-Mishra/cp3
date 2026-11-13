pub fn b48(x: i64) -> i64 {
    let mut acc = x + 48;
    acc = (acc * 31 + 336) % 1000033;
    acc = (acc * 131 + x * 17 + 48) % 1000199;
    acc
}
