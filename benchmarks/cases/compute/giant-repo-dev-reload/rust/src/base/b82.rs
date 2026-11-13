pub fn b82(x: i64) -> i64 {
    let mut acc = x + 82;
    acc = (acc * 31 + 574) % 1000033;
    acc = (acc * 131 + x * 17 + 82) % 1000199;
    acc
}
