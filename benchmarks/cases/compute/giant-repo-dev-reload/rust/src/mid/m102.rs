use crate::base::b50::b50;
use crate::base::b90::b90;

pub fn m102(x: i64) -> i64 {
    b50(x) + b90(x + 1)
}

