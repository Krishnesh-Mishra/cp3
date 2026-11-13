pub fn b108(x: i64) -> i64 {
    let mut acc = x + 108;
    acc = (acc * 31 + 756) % 1000033;
    acc = (acc * 131 + x * 17 + 108) % 1000199;
    acc
}
