use crate::base::b101::b101;
use crate::base::b13::b13;

pub fn m119(x: i64) -> i64 {
    b101(x) + b13(x + 1)
}

