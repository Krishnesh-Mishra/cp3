use crate::base::b16::b16;
use crate::base::b56::b56;

pub fn m48(x: i64) -> i64 {
    b16(x) + b56(x + 1)
}

