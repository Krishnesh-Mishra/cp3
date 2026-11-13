use crate::base::b59::b59;
use crate::base::b99::b99;

pub fn m105(x: i64) -> i64 {
    b59(x) + b99(x + 1)
}

