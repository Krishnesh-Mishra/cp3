pub fn b97(x: i64) -> i64 {
    let mut acc = x + 97;
    acc = (acc * 31 + 679) % 1000033;
    acc = (acc * 131 + x * 17 + 97) % 1000199;
    acc
}
