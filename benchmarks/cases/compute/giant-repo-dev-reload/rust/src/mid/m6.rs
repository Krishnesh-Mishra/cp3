use crate::base::b18::b18;
use crate::base::b58::b58;

pub fn m6(x: i64) -> i64 {
    b18(x) + b58(x + 1)
}

