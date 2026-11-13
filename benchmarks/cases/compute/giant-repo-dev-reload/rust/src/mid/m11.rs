use crate::base::b33::b33;
use crate::base::b73::b73;

pub fn m11(x: i64) -> i64 {
    b33(x) + b73(x + 1)
}

