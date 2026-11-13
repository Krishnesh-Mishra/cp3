pub fn b57(x: i64) -> i64 {
    let mut acc = x + 57;
    acc = (acc * 31 + 399) % 1000033;
    acc = (acc * 131 + x * 17 + 57) % 1000199;
    acc
}
