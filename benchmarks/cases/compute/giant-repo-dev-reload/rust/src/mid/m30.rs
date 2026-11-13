use crate::base::b90::b90;
use crate::base::b2::b2;

pub fn m30(x: i64) -> i64 {
    b90(x) + b2(x + 1)
}

