pub fn b102(x: i64) -> i64 {
    let mut acc = x + 102;
    acc = (acc * 31 + 714) % 1000033;
    acc = (acc * 131 + x * 17 + 102) % 1000199;
    acc
}
