use crate::base::b81::b81;
use crate::base::b121::b121;

pub fn m27(x: i64) -> i64 {
    b81(x) + b121(x + 1)
}

