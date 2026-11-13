use crate::base::b19::b19;
use crate::base::b59::b59;

pub fn m49(x: i64) -> i64 {
    b19(x) + b59(x + 1)
}

