pub fn b94(x: i64) -> i64 {
    let mut acc = x + 94;
    acc = (acc * 31 + 658) % 1000033;
    acc = (acc * 131 + x * 17 + 94) % 1000199;
    acc
}
