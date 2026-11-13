pub fn b117(x: i64) -> i64 {
    let mut acc = x + 117;
    acc = (acc * 31 + 819) % 1000033;
    acc = (acc * 131 + x * 17 + 117) % 1000199;
    acc
}
