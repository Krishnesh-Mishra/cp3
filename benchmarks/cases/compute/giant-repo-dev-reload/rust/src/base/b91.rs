pub fn b91(x: i64) -> i64 {
    let mut acc = x + 91;
    acc = (acc * 31 + 637) % 1000033;
    acc = (acc * 131 + x * 17 + 91) % 1000199;
    acc
}
