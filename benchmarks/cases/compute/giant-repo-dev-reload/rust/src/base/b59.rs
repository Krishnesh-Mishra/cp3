pub fn b59(x: i64) -> i64 {
    let mut acc = x + 59;
    acc = (acc * 31 + 413) % 1000033;
    acc = (acc * 131 + x * 17 + 59) % 1000199;
    acc
}
