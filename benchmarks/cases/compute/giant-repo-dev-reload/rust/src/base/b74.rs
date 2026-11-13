pub fn b74(x: i64) -> i64 {
    let mut acc = x + 74;
    acc = (acc * 31 + 518) % 1000033;
    acc = (acc * 131 + x * 17 + 74) % 1000199;
    acc
}
