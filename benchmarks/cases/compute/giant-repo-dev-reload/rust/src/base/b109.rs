pub fn b109(x: i64) -> i64 {
    let mut acc = x + 109;
    acc = (acc * 31 + 763) % 1000033;
    acc = (acc * 131 + x * 17 + 109) % 1000199;
    acc
}
