use crate::base::b44::b44;
use crate::base::b84::b84;

pub fn m100(x: i64) -> i64 {
    b44(x) + b84(x + 1)
}

