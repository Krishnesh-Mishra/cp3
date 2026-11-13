use crate::base::b84::b84;
use crate::base::b124::b124;

pub fn m28(x: i64) -> i64 {
    b84(x) + b124(x + 1)
}

