use crate::base::b116::b116;
use crate::base::b28::b28;

pub fn m124(x: i64) -> i64 {
    b116(x) + b28(x + 1)
}

