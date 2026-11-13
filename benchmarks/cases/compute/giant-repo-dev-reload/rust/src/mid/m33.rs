use crate::base::b99::b99;
use crate::base::b11::b11;

pub fn m33(x: i64) -> i64 {
    b99(x) + b11(x + 1)
}

