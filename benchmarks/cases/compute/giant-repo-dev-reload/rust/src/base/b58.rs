pub fn b58(x: i64) -> i64 {
    let mut acc = x + 58;
    acc = (acc * 31 + 406) % 1000033;
    acc = (acc * 131 + x * 17 + 58) % 1000199;
    acc
}
