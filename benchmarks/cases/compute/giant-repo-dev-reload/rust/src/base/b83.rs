pub fn b83(x: i64) -> i64 {
    let mut acc = x + 83;
    acc = (acc * 31 + 581) % 1000033;
    acc = (acc * 131 + x * 17 + 83) % 1000199;
    acc
}
