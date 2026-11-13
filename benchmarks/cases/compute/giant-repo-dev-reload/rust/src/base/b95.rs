pub fn b95(x: i64) -> i64 {
    let mut acc = x + 95;
    acc = (acc * 31 + 665) % 1000033;
    acc = (acc * 131 + x * 17 + 95) % 1000199;
    acc
}
