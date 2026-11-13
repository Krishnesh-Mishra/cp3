pub fn b37(x: i64) -> i64 {
    let mut acc = x + 37;
    acc = (acc * 31 + 259) % 1000033;
    acc = (acc * 131 + x * 17 + 37) % 1000199;
    acc
}
