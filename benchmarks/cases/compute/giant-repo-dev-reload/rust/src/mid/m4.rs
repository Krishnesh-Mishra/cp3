use crate::base::b12::b12;
use crate::base::b52::b52;

pub fn m4(x: i64) -> i64 {
    b12(x) + b52(x + 1)
}

