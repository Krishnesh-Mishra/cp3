pub fn b111(x: i64) -> i64 {
    let mut acc = x + 111;
    acc = (acc * 31 + 777) % 1000033;
    acc = (acc * 131 + x * 17 + 111) % 1000199;
    acc
}
