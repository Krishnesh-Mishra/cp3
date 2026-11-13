use crate::base::b2::b2;
use crate::base::b42::b42;

pub fn m86(x: i64) -> i64 {
    b2(x) + b42(x + 1)
}

