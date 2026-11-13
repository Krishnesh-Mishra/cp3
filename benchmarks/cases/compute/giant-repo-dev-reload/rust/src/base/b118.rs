pub fn b118(x: i64) -> i64 {
    let mut acc = x + 118;
    acc = (acc * 31 + 826) % 1000033;
    acc = (acc * 131 + x * 17 + 118) % 1000199;
    acc
}
