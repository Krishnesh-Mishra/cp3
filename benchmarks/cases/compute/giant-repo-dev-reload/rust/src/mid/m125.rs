use crate::base::b119::b119;
use crate::base::b31::b31;

pub fn m125(x: i64) -> i64 {
    b119(x) + b31(x + 1)
}

