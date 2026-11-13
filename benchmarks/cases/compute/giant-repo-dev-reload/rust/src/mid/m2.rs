use crate::base::b6::b6;
use crate::base::b46::b46;

pub fn m2(x: i64) -> i64 {
    b6(x) + b46(x + 1)
}

