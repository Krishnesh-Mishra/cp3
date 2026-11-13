use crate::base::b126::b126;
use crate::base::b38::b38;

pub fn m42(x: i64) -> i64 {
    b126(x) + b38(x + 1)
}

