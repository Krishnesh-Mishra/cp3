use crate::base::b46::b46;
use crate::base::b86::b86;

pub fn m58(x: i64) -> i64 {
    b46(x) + b86(x + 1)
}

