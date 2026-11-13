use crate::base::b86::b86;
use crate::base::b126::b126;

pub fn m114(x: i64) -> i64 {
    b86(x) + b126(x + 1)
}

