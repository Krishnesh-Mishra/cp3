pub fn b64(x: i64) -> i64 {
    let mut acc = x + 64;
    acc = (acc * 31 + 448) % 1000033;
    acc = (acc * 131 + x * 17 + 64) % 1000199;
    acc
}
