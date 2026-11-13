use crate::base::b76::b76;
use crate::base::b116::b116;

pub fn m68(x: i64) -> i64 {
    b76(x) + b116(x + 1)
}

