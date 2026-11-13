use crate::base::b52::b52;
use crate::base::b92::b92;

pub fn m60(x: i64) -> i64 {
    b52(x) + b92(x + 1)
}

