pub fn b71(x: i64) -> i64 {
    let mut acc = x + 71;
    acc = (acc * 31 + 497) % 1000033;
    acc = (acc * 131 + x * 17 + 71) % 1000199;
    acc
}
