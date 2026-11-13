pub fn b115(x: i64) -> i64 {
    let mut acc = x + 115;
    acc = (acc * 31 + 805) % 1000033;
    acc = (acc * 131 + x * 17 + 115) % 1000199;
    acc
}
