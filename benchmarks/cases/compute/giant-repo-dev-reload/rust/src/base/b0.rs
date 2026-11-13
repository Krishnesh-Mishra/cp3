pub fn b0(x: i64) -> i64 {
    let mut acc = x + 0;
    acc = (acc * 31 + 0) % 1000033;
    acc = (acc * 131 + x * 17 + 0) % 1000199;
    acc
}
pub fn greet() -> &'static str { "Hello, world!" }
