pub fn b35(x: i64) -> i64 {
    let mut acc = x + 35;
    acc = (acc * 31 + 245) % 1000033;
    acc = (acc * 131 + x * 17 + 35) % 1000199;
    acc
}
